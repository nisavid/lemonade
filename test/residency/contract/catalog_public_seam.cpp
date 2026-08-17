#include "lemon/residency/catalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#ifndef RESIDENCY_CATALOG_PATH
#define RESIDENCY_CATALOG_PATH ""
#endif

namespace {

using json = nlohmann::json;
using namespace lemon::residency;

constexpr std::string_view SOURCE_SUPPORT_BASELINE =
    "a505bbc702cc1fcd44ef73c44defabc98c36d505";

template <typename T, typename = void>
struct has_snapshot_member : std::false_type {};

template <typename T>
struct has_snapshot_member<
    T,
    std::void_t<decltype(std::declval<T>().snapshot)>> : std::true_type {};

template <typename T, typename = void>
struct has_action_authority_method : std::false_type {};

template <typename T>
struct has_action_authority_method<
    T,
    std::void_t<decltype(std::declval<T>().authorizes_action())>>
    : std::true_type {};

static_assert(!has_snapshot_member<CatalogValidationResult>::value);
static_assert(!has_action_authority_method<CatalogSelection>::value);

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "packaged catalog could not be opened");
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

json& unit_by_id(json& document, std::string_view id) {
    for (auto& unit : document.at("promotion_units")) {
        if (unit.at("id").get<std::string>() == id) {
            return unit;
        }
    }
    fail("fixture promotion unit is unavailable");
}

std::string render(const json& document) {
    return document.dump(2) + '\n';
}

void require_validation_status(const CatalogValidationResult& result,
                               CatalogLoadStatus expected,
                               std::string_view label) {
    if (result.status != expected) {
        fail(std::string(label) + ": " + result.diagnostic);
    }
    if (expected == CatalogLoadStatus::Accepted) {
        require(result.diagnostic.empty(), "accepted catalog has a diagnostic");
    } else {
        require(!result.diagnostic.empty(), "rejected catalog omitted its diagnostic");
    }
}

void require_load_rejection(const CatalogLoadResult& result,
                            CatalogLoadStatus expected,
                            std::string_view label) {
    require(result.status == expected, label);
    require(!result.accepted(), "rejected catalog reported accepted");
    require(!result.snapshot.has_value(), "rejected catalog exposed a snapshot");
    require(!result.diagnostic.empty(), "rejected catalog omitted its diagnostic");
}

void require_fallback(const CatalogSelection& selection,
                      std::string_view condition,
                      std::string_view expected) {
    const auto found = selection.fallbacks.find(std::string(condition));
    require(found != selection.fallbacks.end(), "selected fallback condition is missing");
    require(found->second.token() == expected, "selected fallback identifier is wrong");
}

void require_constraints(
    const CatalogSelection& selection,
    std::initializer_list<ConstraintKind> expected) {
    require(
        selection.constraints.size() == expected.size(),
        "selected affected-constraint set has the wrong size");
    for (const auto constraint : expected) {
        require(
            std::find(
                selection.constraints.begin(),
                selection.constraints.end(),
                constraint) != selection.constraints.end(),
            "selected affected-constraint set is incomplete");
    }
}

void require_compatibility_reference(
    const CatalogSelection& selection,
    std::string_view expected) {
    require(
        selection.compatibility_contract_ids.size() == 1,
        "selected compatibility-contract set has the wrong size");
    require(
        selection.compatibility_contract_ids.front().token() == expected,
        "selected compatibility-contract identity is wrong");
}

void require_sha256(std::string_view value, std::string_view label) {
    require(value.size() == 64, label);
    require(
        std::all_of(
            value.begin(),
            value.end(),
            [](char character) {
                return (character >= '0' && character <= '9') ||
                       (character >= 'a' && character <= 'f');
            }),
        label);
}

RuntimeCatalogSelector rocm_admission_selector() {
    RuntimeCatalogSelector selector;
    selector.source_support_baseline = SOURCE_SUPPORT_BASELINE;
    selector.base_variant = "llamacpp-rocm";
    selector.platform = "linux-amd-rocm-llamacpp";
    selector.backend_channel = "stable";
    selector.model_type = "llm";
    selector.operation_template = OperationTemplate::Adm;
    selector.operation_kind = OperationKind::Admission;
    selector.constraints = {
        ConstraintKind::GpuSharedResidency,
        ConstraintKind::HostMemAvailableFloor,
        ConstraintKind::ModelTypePool,
        ConstraintKind::Ownership,
    };
    selector.recovery = "native_subprocess_tree";
    selector.material_profiles = {
        {"configuration_profile", "profile-free-residency-estimation-v1-text-only"},
        {"hardware_profile", "hatchery-gfx1151-shared-gtt-v1"},
        {"observation_contract", "hatchery-gtt-host-observation-v1"},
        {"predictor_rule", "hatchery-llamacpp-rocm-profile-free-v1"},
        {"workload_profile", "hatchery-text-generation-campaign-v1"},
    };
    return selector;
}

RuntimeCatalogSelector windows_flm_startup_selector() {
    RuntimeCatalogSelector selector;
    selector.source_support_baseline = SOURCE_SUPPORT_BASELINE;
    selector.base_variant = "flm-npu";
    selector.platform = "windows-xdna2";
    selector.backend_channel = "single";
    selector.model_type = "llm";
    selector.operation_template = OperationTemplate::Sta;
    selector.operation_kind = OperationKind::StartupLoad;
    selector.constraints = {
        ConstraintKind::FlmTypeSlot,
        ConstraintKind::NpuCrossFamily,
        ConstraintKind::ModelTypePool,
        ConstraintKind::Ownership,
    };
    selector.recovery = "flm_system_managed";
    return selector;
}

CompatibilityCatalogSelector npu_compatibility_selector() {
    CompatibilityCatalogSelector selector;
    selector.source_support_baseline = SOURCE_SUPPORT_BASELINE;
    selector.platform = "windows-xdna2";
    selector.coexist_by_type_variant = "flm-npu";
    selector.exclusive_variant = "whispercpp-npu";
    selector.direction = "exclusive_incoming";
    selector.incumbent_state = "pinned";
    selector.constraints = {ConstraintKind::NpuCrossFamily};
    return selector;
}

void require_inactive_selection(const CatalogSelection& selection,
                                std::string_view expected_id,
                                CapabilityLevel expected_evidence_ceiling) {
    require(
        selection.status == CatalogSelectionStatus::Unsupported,
        "inactive catalog match was not rejected as unsupported");
    require(
        selection.candidate_promotion_unit_id.has_value(),
        "unsupported match omitted its strong promotion-unit identity");
    require(
        selection.candidate_promotion_unit_id->token() == expected_id,
        "unsupported match selected the wrong promotion unit");
    require(
        !selection.effective_promotion_unit_id.has_value(),
        "unsupported match exposed an effective catalog rule");
    require(
        selection.capability_level == CapabilityLevel::Unsupported,
        "unsupported match raised capability");
    require(
        selection.evidence_ceiling == expected_evidence_ceiling,
        "unsupported match changed its evidence ceiling");
    require(
        selection.delivery_state == DeliveryState::Absent,
        "unsupported match raised delivery");
}

void require_missing_selection(const CatalogSelection& selection,
                               std::string_view label) {
    require(selection.status == CatalogSelectionStatus::Missing, label);
    require(
        !selection.candidate_promotion_unit_id.has_value(),
        "missing selector exposed a promotion unit");
    require(
        !selection.effective_promotion_unit_id.has_value(),
        "missing selector exposed an effective catalog rule");
}

void require_packaged_catalog(const std::string& catalog_bytes) {
    require_sha256(
        packaged_catalog_sha256,
        "packaged catalog digest is not lowercase SHA-256");

    const json canonical = json::parse(catalog_bytes);
    require(
        canonical.at("source_support_baseline") == SOURCE_SUPPORT_BASELINE,
        "catalog omitted its source-support baseline");
    const auto selection_registry_sha256 =
        canonical.at("selection_registry_sha256").get<std::string>();
    require_sha256(
        selection_registry_sha256,
        "selection-registry digest is not lowercase SHA-256");

    const auto semantic = validate_catalog_document(catalog_bytes);
    require_validation_status(
        semantic,
        CatalogLoadStatus::Accepted,
        "canonical catalog failed semantic validation");

    const auto packaged = load_packaged_catalog(catalog_bytes);
    require(packaged.status == CatalogLoadStatus::Accepted, packaged.diagnostic);
    require(packaged.accepted(), "packaged catalog failed its digest gate");
    require(packaged.snapshot.has_value(), "packaged catalog omitted its snapshot");
    require(
        packaged.snapshot->catalog_sha256() == packaged_catalog_sha256,
        "snapshot catalog digest differs from the generated lock");
    require(
        packaged.snapshot->source_support_baseline() == SOURCE_SUPPORT_BASELINE,
        "snapshot source-support baseline drifted");
    require(
        packaged.snapshot->selection_registry_sha256() ==
            selection_registry_sha256,
        "snapshot selection-registry digest drifted");

    const auto rocm = packaged.snapshot->resolve(rocm_admission_selector());
    require_inactive_selection(
        rocm,
        "H-ROCM-ADM-GTT-HOST-v1",
        CapabilityLevel::Validated);
    require_fallback(
        rocm,
        "insufficient_capacity_authority",
        "hatchery_rocm_admission_refuse_unknown_capacity_v1");
    require_constraints(
        rocm,
        {
            ConstraintKind::GpuSharedResidency,
            ConstraintKind::HostMemAvailableFloor,
            ConstraintKind::ModelTypePool,
            ConstraintKind::Ownership,
        });
    require(
        rocm.compatibility_contract_ids.empty(),
        "ROCm cell acquired an unrelated compatibility contract");

    const auto windows = packaged.snapshot->resolve(windows_flm_startup_selector());
    require_inactive_selection(
        windows,
        "W-XDNA2-FLM-NPU-LLM-STA-v1",
        CapabilityLevel::Modeled);
    require_fallback(
        windows,
        "insufficient_startup_authority",
        "residency_startup_block_group_v1");
    require_constraints(
        windows,
        {
            ConstraintKind::FlmTypeSlot,
            ConstraintKind::NpuCrossFamily,
            ConstraintKind::ModelTypePool,
            ConstraintKind::Ownership,
        });
    require_compatibility_reference(
        windows,
        "H-NPU-FLM-CONFLICT-XDNA2-v1");

    const auto compatibility = packaged.snapshot->resolve_compatibility(
        npu_compatibility_selector());
    require_inactive_selection(
        compatibility,
        "H-NPU-FLM-CONFLICT-XDNA2-v1",
        CapabilityLevel::Modeled);
    require_fallback(
        compatibility,
        "insufficient_displacement_authority",
        "residency_npu_conflict_preserve_refuse_v1");
    require_constraints(
        compatibility,
        {ConstraintKind::NpuCrossFamily});

    auto runtime_npc = windows_flm_startup_selector();
    runtime_npc.operation_template = OperationTemplate::Npc;
    runtime_npc.operation_kind = OperationKind::Admission;
    require_missing_selection(
        packaged.snapshot->resolve(runtime_npc),
        "runtime resolution returned an evidence-only compatibility contract");

    auto invalid_compatibility = npu_compatibility_selector();
    invalid_compatibility.incumbent_state = "future_state";
    require_missing_selection(
        packaged.snapshot->resolve_compatibility(invalid_compatibility),
        "unknown compatibility state did not fail closed");

    auto partial = rocm_admission_selector();
    partial.recovery.clear();
    require_missing_selection(
        packaged.snapshot->resolve(partial),
        "partial selector did not fail closed");

    auto unmatched = rocm_admission_selector();
    unmatched.base_variant = "llamacpp-metal";
    require_missing_selection(
        packaged.snapshot->resolve(unmatched),
        "unmatched selector did not fail closed");

    auto empty_baseline = rocm_admission_selector();
    empty_baseline.source_support_baseline.clear();
    require_missing_selection(
        packaged.snapshot->resolve(empty_baseline),
        "empty source baseline did not fail closed");

    auto wrong_baseline = rocm_admission_selector();
    wrong_baseline.source_support_baseline =
        "0000000000000000000000000000000000000000";
    require_missing_selection(
        packaged.snapshot->resolve(wrong_baseline),
        "wrong source baseline did not fail closed");

    auto wrong_constraints = rocm_admission_selector();
    wrong_constraints.constraints = {ConstraintKind::GpuSharedResidency};
    require_missing_selection(
        packaged.snapshot->resolve(wrong_constraints),
        "wrong affected-constraint set did not fail closed");
}

void require_parser_rejections(const std::string& catalog_bytes) {
    require_validation_status(
        validate_catalog_document("{"),
        CatalogLoadStatus::Malformed,
        "malformed JSON was accepted");

    std::string duplicate_key = catalog_bytes;
    const auto root = duplicate_key.find("{\n");
    require(root != std::string::npos, "canonical catalog root is unavailable");
    duplicate_key.insert(
        root + 2,
        "  \"schema\": \"residency.profiles/1.0\",\n");
    require_validation_status(
        validate_catalog_document(duplicate_key),
        CatalogLoadStatus::Duplicate,
        "duplicate JSON key was accepted");

    std::string nested_duplicate = catalog_bytes;
    const auto nested_key = nested_duplicate.find("\"unit_kind\":");
    require(nested_key != std::string::npos, "nested catalog object is unavailable");
    nested_duplicate.insert(
        nested_key,
        "\"unit_kind\": \"compatibility_contract\",\n      ");
    require_validation_status(
        validate_catalog_document(nested_duplicate),
        CatalogLoadStatus::Duplicate,
        "nested duplicate JSON key was accepted");

    const json accepted = json::parse(catalog_bytes);

    json missing_root = accepted;
    missing_root.erase("generator_version");
    require_validation_status(
        validate_catalog_document(render(missing_root)),
        CatalogLoadStatus::Missing,
        "missing root field was accepted");

    json unknown_root = accepted;
    unknown_root["future_field"] = true;
    require_validation_status(
        validate_catalog_document(render(unknown_root)),
        CatalogLoadStatus::Malformed,
        "unknown root field was accepted");

    json wrong_schema = accepted;
    wrong_schema["schema"] = "residency.profiles/1.1";
    require_validation_status(
        validate_catalog_document(render(wrong_schema)),
        CatalogLoadStatus::Malformed,
        "unknown catalog schema version was accepted");

    json wrong_baseline_width = accepted;
    wrong_baseline_width["source_support_baseline"] = std::string(64, '0');
    require_validation_status(
        validate_catalog_document(render(wrong_baseline_width)),
        CatalogLoadStatus::Malformed,
        "noncanonical source-support baseline width was accepted");

    json missing_roster_member = accepted;
    missing_roster_member.at("promotion_units").erase(
        missing_roster_member.at("promotion_units").begin());
    require_validation_status(
        validate_catalog_document(render(missing_roster_member)),
        CatalogLoadStatus::Missing,
        "incomplete promotion roster was accepted");

    json missing_unit = accepted;
    missing_unit.at("promotion_units").at(0).erase("contract");
    require_validation_status(
        validate_catalog_document(render(missing_unit)),
        CatalogLoadStatus::Missing,
        "missing unit field was accepted");

    json duplicate_unit = accepted;
    duplicate_unit.at("promotion_units").at(1) =
        duplicate_unit.at("promotion_units").at(0);
    require_validation_status(
        validate_catalog_document(render(duplicate_unit)),
        CatalogLoadStatus::Duplicate,
        "duplicate promotion-unit identity was accepted");

    json unknown_kind = accepted;
    unknown_kind.at("promotion_units").at(0)["unit_kind"] = "future_kind";
    require_validation_status(
        validate_catalog_document(render(unknown_kind)),
        CatalogLoadStatus::Malformed,
        "unknown promotion-unit kind was accepted");

    json swapped_kinds = accepted;
    auto& swapped_compatibility =
        unit_by_id(swapped_kinds, "H-NPU-FLM-CONFLICT-XDNA2-v1");
    auto& swapped_runtime =
        unit_by_id(swapped_kinds, "H-ROCM-ADM-GTT-HOST-v1");
    swapped_compatibility["id"] = "H-ROCM-ADM-GTT-HOST-v1";
    swapped_compatibility["contract"]["contract_id"] =
        "H-ROCM-ADM-GTT-HOST-v1";
    swapped_runtime["id"] = "H-NPU-FLM-CONFLICT-XDNA2-v1";
    swapped_runtime["contract"]["cell_id"] =
        "H-NPU-FLM-CONFLICT-XDNA2-v1";
    for (auto& unit : swapped_kinds.at("promotion_units")) {
        auto& contract = unit.at("contract");
        if (!contract.contains("compatibility_contracts")) {
            continue;
        }
        for (auto& reference : contract.at("compatibility_contracts")) {
            if (reference == "H-NPU-FLM-CONFLICT-XDNA2-v1") {
                reference = "H-ROCM-ADM-GTT-HOST-v1";
            }
        }
    }
    require_validation_status(
        validate_catalog_document(render(swapped_kinds)),
        CatalogLoadStatus::Malformed,
        "promotion-unit IDs were accepted under the wrong kinds");

    json unknown_selector_field = accepted;
    unit_by_id(unknown_selector_field, "W-XDNA2-FLM-NPU-LLM-STA-v1")
        ["contract"]["selector"]["future_field"] = "ignored";
    require_validation_status(
        validate_catalog_document(render(unknown_selector_field)),
        CatalogLoadStatus::Malformed,
        "unknown nested selector field was accepted");

    json mismatched_identity = accepted;
    unit_by_id(mismatched_identity, "H-ROCM-ADM-GTT-HOST-v1")
        ["contract"]["cell_id"] = "H-ROCM-PRE-GTT-HOST-v1";
    require_validation_status(
        validate_catalog_document(render(mismatched_identity)),
        CatalogLoadStatus::Malformed,
        "outer and inner promotion-unit identities may disagree");

    json mismatched_state = accepted;
    unit_by_id(mismatched_state, "H-ROCM-ADM-GTT-HOST-v1")
        ["contract"]["capability_level"] = "modeled";
    require_validation_status(
        validate_catalog_document(render(mismatched_state)),
        CatalogLoadStatus::Malformed,
        "outer and inner capability state may disagree");

    json exact_above_ceiling = accepted;
    auto& exact_above =
        unit_by_id(exact_above_ceiling, "H-ROCM-ADM-GTT-HOST-v1");
    exact_above["capability_level"] = "validated";
    exact_above["delivery_state"] = "release_verified";
    exact_above["contract"]["capability_level"] = "validated";
    exact_above["contract"]["delivery_state"] = "release_verified";
    exact_above["contract"]["evidence_ceiling"] = "modeled";
    require_validation_status(
        validate_catalog_document(render(exact_above_ceiling)),
        CatalogLoadStatus::Malformed,
        "exact-cell capability exceeded its evidence ceiling");

    json later_without_delivery = accepted;
    auto& later_absent = unit_by_id(
        later_without_delivery,
        "W-XDNA2-FLM-NPU-LLM-STA-v1");
    later_absent["capability_level"] = "modeled";
    later_absent["contract"]["initial_state"]["capability_level"] =
        "modeled";
    require_validation_status(
        validate_catalog_document(render(later_without_delivery)),
        CatalogLoadStatus::Malformed,
        "later-runtime capability was accepted while delivery was absent");

    json compatibility_above_ceiling = accepted;
    auto& compatibility_above = unit_by_id(
        compatibility_above_ceiling,
        "H-NPU-FLM-CONFLICT-XDNA2-v1");
    compatibility_above["capability_level"] = "validated";
    compatibility_above["delivery_state"] = "release_verified";
    compatibility_above["contract"]["capability_level"] = "validated";
    compatibility_above["contract"]["delivery_state"] = "release_verified";
    require_validation_status(
        validate_catalog_document(render(compatibility_above_ceiling)),
        CatalogLoadStatus::Malformed,
        "compatibility capability exceeded its evidence ceiling");

    json unknown_fallback = accepted;
    unit_by_id(unknown_fallback, "H-ROCM-ADM-GTT-HOST-v1")
        ["contract"]["fallbacks"]["insufficient_capacity_authority"] =
        "future_fallback";
    require_validation_status(
        validate_catalog_document(render(unknown_fallback)),
        CatalogLoadStatus::Malformed,
        "unknown fallback reference was accepted");

    json invalid_template_leaf = accepted;
    unit_by_id(invalid_template_leaf, "W-XDNA2-FLM-NPU-LLM-STA-v1")
        ["contract"]["selector"]["operation_template"] = "ADM";
    require_validation_status(
        validate_catalog_document(render(invalid_template_leaf)),
        CatalogLoadStatus::Malformed,
        "invalid template-to-operation relationship was accepted");

    json ambiguous = accepted;
    auto& ambiguous_left =
        unit_by_id(ambiguous, "W-XDNA2-FLM-NPU-EMBEDDING-ADM-v1");
    auto& ambiguous_right =
        unit_by_id(ambiguous, "W-XDNA2-FLM-NPU-LLM-ADM-v1");
    ambiguous_right["contract"]["selector"] =
        ambiguous_left["contract"]["selector"];
    ambiguous_right["contract"]["material_profiles"] =
        ambiguous_left["contract"]["material_profiles"];
    require_validation_status(
        validate_catalog_document(render(ambiguous)),
        CatalogLoadStatus::Ambiguous,
        "identical predicates with distinct identities were accepted");

    json incomparable = accepted;
    auto& incomparable_left =
        unit_by_id(incomparable, "W-XDNA2-FLM-NPU-EMBEDDING-ADM-v1");
    auto& incomparable_right =
        unit_by_id(incomparable, "W-XDNA2-FLM-NPU-LLM-ADM-v1");
    incomparable_right["contract"]["selector"] =
        incomparable_left["contract"]["selector"];
    incomparable_left["contract"]["material_profiles"] = {
        {"hardware_profile", "hatchery-gfx1151-vulkan-shared-gtt-v1"}};
    incomparable_right["contract"]["material_profiles"] = {
        {"configuration_profile",
         "profile-free-residency-estimation-vulkan-v1-text-only"}};
    require_validation_status(
        validate_catalog_document(render(incomparable)),
        CatalogLoadStatus::PrecedenceIncomparable,
        "orthogonal overlapping predicates were accepted");

    json undeclared_precedence = accepted;
    auto& broad = unit_by_id(
        undeclared_precedence,
        "W-XDNA2-FLM-NPU-EMBEDDING-ADM-v1");
    auto& narrow = unit_by_id(
        undeclared_precedence,
        "W-XDNA2-FLM-NPU-LLM-ADM-v1");
    narrow["contract"]["selector"] = broad["contract"]["selector"];
    broad["contract"]["material_profiles"] = json::object();
    narrow["contract"]["material_profiles"] = {
        {"hardware_profile", "hatchery-gfx1151-vulkan-shared-gtt-v1"}};
    require_validation_status(
        validate_catalog_document(render(undeclared_precedence)),
        CatalogLoadStatus::Ambiguous,
        "undeclared broad-to-narrow precedence was accepted");
}

void require_digest_gate(const std::string& catalog_bytes) {
    std::string mutated = catalog_bytes;
    require(!mutated.empty(), "packaged catalog is empty");
    require(mutated.back() == '\n', "packaged catalog is not newline terminated");
    mutated.back() = ' ';

    require_validation_status(
        validate_catalog_document(mutated),
        CatalogLoadStatus::Accepted,
        "semantic validator applied the packaged digest gate");

    require_load_rejection(
        load_packaged_catalog(mutated),
        CatalogLoadStatus::DigestMismatch,
        "one-byte packaged catalog mutation was accepted");

    require_load_rejection(
        load_packaged_catalog("{"),
        CatalogLoadStatus::DigestMismatch,
        "tampered malformed catalog was parsed before its digest gate");
}

}

int run_residency_catalog_public_seam(int argc, char** argv) {
    std::string path = RESIDENCY_CATALOG_PATH;
    if (argc == 2) {
        path = argv[1];
    }
    require(!path.empty(), "packaged catalog path is unavailable");

    const auto catalog_bytes = read_file(path);
    require_packaged_catalog(catalog_bytes);
    require_parser_rejections(catalog_bytes);
    require_digest_gate(catalog_bytes);
    return 0;
}

#ifndef RESIDENCY_CATALOG_SEAM_NO_MAIN
int main(int argc, char** argv) {
    return run_residency_catalog_public_seam(argc, argv);
}
#endif
