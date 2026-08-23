#define RESIDENCY_CATALOG_SEAM_NO_MAIN
#include "../residency/contract/catalog_public_seam.cpp"
#undef RESIDENCY_CATALOG_SEAM_NO_MAIN

#include "lemon/residency/explanations.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace lemon::residency;

constexpr std::string_view expected_catalog_sha256 =
    "d11c2011462a8b2d2f83d557ee6e535e506c2e2d36a30a22875ceb743aa78485";

template <typename T>
T known_value(const DecodedValue<T> &decoded, std::string_view label) {
    require(decoded.is_known(), label);
    return *decoded.known_value();
}

std::vector<ConstraintKind> constraints_from(const json &rows) {
    std::vector<ConstraintKind> result;
    for (const auto &row : rows) {
        result.push_back(
            known_value(decode_constraint_kind(row.get<std::string>()),
                        "catalog constraint is unknown to generated C++"));
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::map<std::string, std::string> string_map(const json &object) {
    std::map<std::string, std::string> result;
    for (const auto &[key, value] : object.items()) {
        result.emplace(key, value.get<std::string>());
    }
    return result;
}

void require_selection_matches(const CatalogSelection &selection,
                               const json &unit) {
    const auto &contract = unit.at("contract");
    const auto expected_id = unit.at("id").get<std::string>();
    require(selection.status == CatalogSelectionStatus::Unsupported,
            "inactive selector did not remain unsupported");
    require(selection.candidate_promotion_unit_id.has_value() &&
                selection.candidate_promotion_unit_id->token() == expected_id,
            "selector returned the wrong candidate identity");
    require(!selection.effective_promotion_unit_id.has_value(),
            "inactive selector exposed an effective identity");
    require(selection.capability_level == CapabilityLevel::Unsupported,
            "inactive selector raised capability");
    require(selection.delivery_state == DeliveryState::Absent,
            "inactive selector raised delivery");
    require(
        selection.evidence_ceiling ==
            known_value(decode_capability_level(
                            contract.at("evidence_ceiling").get<std::string>()),
                        "catalog evidence ceiling is unknown to generated C++"),
        "selector changed its evidence ceiling");

    const auto expected_fallbacks = string_map(contract.at("fallbacks"));
    require(selection.fallbacks.size() == expected_fallbacks.size(),
            "selector changed its fallback count");
    for (const auto &[condition, fallback] : expected_fallbacks) {
        const auto found = selection.fallbacks.find(condition);
        require(found != selection.fallbacks.end() &&
                    found->second.token() == fallback,
                "selector changed a fallback binding");
    }

    const auto expected_constraints = constraints_from(
        contract.contains("constraints") ? contract.at("constraints")
                                         : contract.at("relation_constraints"));
    require(selection.constraints == expected_constraints,
            "selector changed its constraint closure");

    std::vector<std::string> expected_compatibility_ids;
    if (contract.contains("compatibility_contracts")) {
        for (const auto &id : contract.at("compatibility_contracts")) {
            expected_compatibility_ids.push_back(id.get<std::string>());
        }
    }
    require(selection.compatibility_contract_ids.size() ==
                expected_compatibility_ids.size(),
            "selector changed its compatibility-reference count");
    for (std::size_t index = 0; index < expected_compatibility_ids.size();
         ++index) {
        require(selection.compatibility_contract_ids[index].token() ==
                    expected_compatibility_ids[index],
                "selector changed a compatibility reference");
    }
}

RuntimeCatalogSelector exact_selector(const json &root, const json &contract,
                                      std::string model_type) {
    const auto &match = contract.at("match");
    RuntimeCatalogSelector selector;
    selector.source_support_baseline =
        root.at("source_support_baseline").get<std::string>();
    selector.base_variant = contract.at("base_variant").get<std::string>();
    selector.platform = match.at("platform").get<std::string>();
    selector.backend_channel = match.at("backend_channel").get<std::string>();
    selector.model_type = std::move(model_type);
    selector.operation_template = known_value(
        decode_operation_template(
            contract.at("operation_template").get<std::string>()),
        "exact-cell operation template is unknown to generated C++");
    selector.operation_kind = known_value(
        decode_operation_kind(contract.at("operation_leaf").get<std::string>()),
        "exact-cell operation kind is unknown to generated C++");
    selector.constraints = constraints_from(contract.at("constraints"));
    selector.recovery = match.at("recovery").get<std::string>();
    for (const auto key : {
             "configuration_profile",
             "hardware_profile",
             "observation_contract",
             "predictor_rule",
             "workload_profile",
         }) {
        selector.material_profiles.emplace(key,
                                           match.at(key).get<std::string>());
    }
    return selector;
}

RuntimeCatalogSelector later_selector(const json &root, const json &contract,
                                      std::string operation_leaf) {
    const auto &row = contract.at("selector");
    RuntimeCatalogSelector selector;
    selector.source_support_baseline =
        root.at("source_support_baseline").get<std::string>();
    selector.base_variant = row.at("base_variant").get<std::string>();
    selector.platform = row.at("platform").get<std::string>();
    selector.backend_channel = row.at("backend_channel").get<std::string>();
    selector.model_type = row.at("model_type").get<std::string>();
    selector.operation_template = known_value(
        decode_operation_template(
            row.at("operation_template").get<std::string>()),
        "later-runtime operation template is unknown to generated C++");
    selector.operation_kind =
        known_value(decode_operation_kind(operation_leaf),
                    "later-runtime operation kind is unknown to generated C++");
    selector.constraints = constraints_from(contract.at("constraints"));
    selector.recovery = contract.at("recovery").get<std::string>();
    selector.material_profiles = string_map(contract.at("material_profiles"));
    return selector;
}

void require_all_runtime_selectors(const CatalogSnapshot &snapshot,
                                   const json &root) {
    std::set<std::string> covered_ids;
    std::size_t selector_cases = 0;
    for (const auto &unit : root.at("promotion_units")) {
        const auto kind = unit.at("unit_kind").get<std::string>();
        const auto &contract = unit.at("contract");
        if (kind == "exact_cell") {
            for (const auto &model_type :
                 contract.at("match").at("model_types")) {
                require_selection_matches(
                    snapshot.resolve(exact_selector(
                        root, contract, model_type.get<std::string>())),
                    unit);
                ++selector_cases;
            }
            covered_ids.emplace(unit.at("id").get<std::string>());
        } else if (kind == "later_runtime") {
            for (const auto &operation_leaf :
                 contract.at("selector").at("operation_leaves")) {
                require_selection_matches(
                    snapshot.resolve(later_selector(
                        root, contract, operation_leaf.get<std::string>())),
                    unit);
                ++selector_cases;
            }
            covered_ids.emplace(unit.at("id").get<std::string>());
        }
    }
    require(covered_ids.size() == 38, "runtime selector roster is incomplete");
    require(selector_cases == 78, "runtime selector leaf matrix drifted");
}

void require_all_compatibility_selectors(const CatalogSnapshot &snapshot,
                                         const json &root) {
    const auto &units = root.at("promotion_units");
    const auto found =
        std::find_if(units.begin(), units.end(), [](const auto &unit) {
            return unit.at("unit_kind") == "compatibility_contract";
        });
    require(found != units.end(), "compatibility contract is unavailable");
    const auto &contract = found->at("contract");
    std::size_t selector_cases = 0;
    for (const auto &platform_case : contract.at("platform_cases")) {
        for (const auto &direction : contract.at("directions")) {
            for (const auto &incumbent_state :
                 contract.at("incumbent_states")) {
                CompatibilityCatalogSelector selector;
                selector.source_support_baseline =
                    root.at("source_support_baseline").get<std::string>();
                selector.platform =
                    platform_case.at("platform").get<std::string>();
                selector.coexist_by_type_variant =
                    platform_case.at("coexist_by_type_variant")
                        .get<std::string>();
                selector.exclusive_variant =
                    platform_case.at("exclusive_variant").get<std::string>();
                selector.direction = direction.get<std::string>();
                selector.incumbent_state = incumbent_state.get<std::string>();
                selector.constraints =
                    constraints_from(contract.at("relation_constraints"));
                require_selection_matches(
                    snapshot.resolve_compatibility(selector), *found);
                ++selector_cases;
            }
        }
    }
    require(selector_cases == 12, "compatibility selector matrix drifted");
}

void require_generated_registries(const json &root) {
    const auto &units = root.at("promotion_units");
    require(units.size() == 39, "promotion-unit count drifted");
    std::set<std::string> promotion_ids;
    for (const auto &unit : units) {
        const auto id = unit.at("id").get<std::string>();
        const auto decoded = decode_promotion_unit_id(id);
        require(decoded.is_known(),
                "catalog promotion unit is unknown to generated C++");
        require(
            promotion_unit_kind(*decoded.known_value()) ==
                known_value(
                    decode_promotion_unit_kind(
                        unit.at("unit_kind").get<std::string>()),
                    "catalog promotion-unit kind is unknown to generated C++"),
            "generated promotion-unit kind drifted");
        require(promotion_ids.emplace(id).second,
                "promotion-unit ID is duplicated");
        require(unit.at("capability_level") == "unsupported" &&
                    unit.at("delivery_state") == "absent",
                "packaged promotion unit became active");
    }

    const auto &fallbacks = root.at("fallbacks");
    require(fallbacks.size() == 14, "fallback count drifted");
    std::set<std::string> fallback_ids;
    for (const auto &fallback : fallbacks) {
        const auto id = fallback.at("id").get<std::string>();
        require(decode_fallback_id(id).is_known(),
                "catalog fallback is unknown to generated C++");
        require(fallback_ids.emplace(id).second, "fallback ID is duplicated");
    }

    const auto &registry = root.at("contract_registry");
    const auto &schemas = registry.at("schema_registry");
    require(schemas.size() == 15, "schema registry count drifted");
    std::set<std::string> schema_ids;
    for (const auto &[internal_key, schema] : schemas.items()) {
        const auto id = schema.at("schema_type").get<std::string>();
        require(decode_schema_type(id).is_known(),
                "catalog schema ID is unknown to generated C++");
        require(!decode_schema_type(internal_key).is_known(),
                "internal schema key escaped onto the wire");
        require(schema_ids.emplace(id).second, "schema ID is duplicated");
    }

    const auto &presentations = registry.at("presentation_registry");
    require(presentations.size() == 27, "presentation registry count drifted");
    for (const auto &[id, row] : presentations.items()) {
        const auto *metadata = reason_presentation_metadata(id);
        require(metadata != nullptr, "generated presentation is missing");
        require(metadata->id == id &&
                    metadata->category_id ==
                        row.at("category_id").get<std::string>() &&
                    metadata->severity ==
                        row.at("severity").get<std::string>() &&
                    metadata->title == row.at("title").get<std::string>() &&
                    metadata->default_message ==
                        row.at("default_message").get<std::string>(),
                "generated presentation metadata drifted");
        require(matching_reason_presentation_for_category(
                    row.at("category_id").get<std::string>(), id) == metadata,
                "generated category-to-presentation mapping drifted");
    }

    const auto &reasons = registry.at("reason_registry");
    require(reasons.size() == 87, "reason registry count drifted");
    for (const auto &[code, row] : reasons.items()) {
        const auto decoded = decode_reason_code(code);
        require(decoded.is_known(),
                "catalog reason is unknown to generated C++");
        const auto *metadata = reason_metadata(decoded);
        const auto &presentation =
            presentations.at(row.at("presentation_id").get<std::string>());
        require(metadata != nullptr, "generated reason metadata is missing");
        require(metadata->code == code &&
                    metadata->category_id ==
                        row.at("category_id").get<std::string>() &&
                    metadata->presentation_id ==
                        row.at("presentation_id").get<std::string>() &&
                    metadata->detail_schema_id ==
                        row.at("detail_schema_id").get<std::string>() &&
                    metadata->severity ==
                        presentation.at("severity").get<std::string>() &&
                    metadata->title ==
                        presentation.at("title").get<std::string>() &&
                    metadata->default_message ==
                        presentation.at("default_message").get<std::string>(),
                "generated reason metadata drifted");

        const WireReasonProjection sender_projection{
            code,
            "sender_category",
            "sender_presentation",
            "sender_severity",
            "Sender title",
            "Sender-controlled message",
        };
        const auto rendered = render_reason_projection(
            explanation_schema_version, explanation_schema_version,
            sender_projection);
        require(rendered.status == ReasonRenderStatus::Known &&
                    rendered.reason.has_value(),
                "known reason did not render through the generated registry");
        require(
            rendered.reason->code == metadata->code &&
                rendered.reason->category_id == metadata->category_id &&
                rendered.reason->presentation_id == metadata->presentation_id &&
                rendered.reason->severity == metadata->severity &&
                rendered.reason->title == metadata->title &&
                rendered.reason->default_message == metadata->default_message,
            "known reason trusted sender-controlled presentation text");
    }

    require(!decode_promotion_unit_id("future-promotion-unit").is_known() &&
                !decode_fallback_id("future_fallback").is_known() &&
                !decode_schema_type("residency.future/1.0").is_known() &&
                !decode_reason_code("residency_future_reason").is_known() &&
                !decode_promotion_unit_kind("future_kind").is_known(),
            "generated registry accepted an unknown wire value");
    require(reason_metadata("residency_future_reason") == nullptr &&
                reason_presentation_metadata("p_future") == nullptr &&
                !reason_category_is_known("future_category"),
            "generated registry exposed metadata for an unknown value");
}

void require_cross_component_matrix(const std::string &catalog_bytes) {
    require(packaged_catalog_sha256 == expected_catalog_sha256,
            "generated catalog digest constant drifted");
    const auto root = json::parse(catalog_bytes);
    const auto loaded = load_packaged_catalog(catalog_bytes);
    require(loaded.accepted(), "packaged catalog could not be loaded");
    require(loaded.snapshot.has_value(),
            "packaged catalog omitted its snapshot");
    require_generated_registries(root);
    require_all_runtime_selectors(*loaded.snapshot, root);
    require_all_compatibility_selectors(*loaded.snapshot, root);
}

}

int main(int argc, char **argv) {
    const auto prior_result =
        lemon::residency::run_residency_catalog_public_seam(argc, argv);
    if (prior_result != 0) {
        return prior_result;
    }
    std::string path = RESIDENCY_CATALOG_PATH;
    if (argc == 2) {
        path = argv[1];
    }
    require(!path.empty(), "packaged catalog path is unavailable");
    require_cross_component_matrix(read_file(path));
    return 0;
}
