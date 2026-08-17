#include "lemon/residency/catalog.h"

#include <mbedtls/md.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace lemon::residency {

namespace catalog_internal {

CatalogSelection make_catalog_selection(
    const PromotionUnitId& id,
    CapabilityLevel capability_level,
    CapabilityLevel evidence_ceiling,
    DeliveryState delivery_state) {
    const bool effective = capability_level != CapabilityLevel::Unsupported &&
                           delivery_state == DeliveryState::ReleaseVerified;
    CatalogSelection selection;
    selection.status = effective ? CatalogSelectionStatus::Resolved
                                 : CatalogSelectionStatus::Unsupported;
    selection.candidate_promotion_unit_id = id;
    if (effective) {
        selection.effective_promotion_unit_id = id;
    }
    selection.capability_level = capability_level;
    selection.evidence_ceiling = evidence_ceiling;
    selection.delivery_state = delivery_state;
    return selection;
}

}

namespace {

using json = nlohmann::json;

constexpr std::size_t max_diagnostic_bytes = 1000;
constexpr std::size_t expected_promotion_unit_count = 39;
constexpr std::size_t expected_fallback_count = 14;

using FallbackRegistry = std::map<std::string, std::vector<OperationTemplate>>;

constexpr std::array<std::string_view, 5> material_profile_keys{{
    "configuration_profile",
    "hardware_profile",
    "observation_contract",
    "predictor_rule",
    "workload_profile",
}};

class CatalogFailure : public std::runtime_error {
public:
    CatalogFailure(CatalogLoadStatus status, std::string message)
        : std::runtime_error(std::move(message)), status_(status) {}

    CatalogLoadStatus status() const noexcept { return status_; }

private:
    CatalogLoadStatus status_;
};

[[noreturn]] void reject(CatalogLoadStatus status, std::string message) {
    throw CatalogFailure(status, std::move(message));
}

std::string bounded_diagnostic(std::string message) {
    if (message.size() > max_diagnostic_bytes) {
        message.resize(max_diagnostic_bytes);
    }
    return message;
}

const json& required(const json& object, std::string_view key, std::string_view label) {
    if (!object.is_object()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " must be an object");
    }
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        reject(
            CatalogLoadStatus::Missing,
            std::string(label) + " is missing " + std::string(key));
    }
    return *found;
}

void require_exact_keys(
    const json& object,
    std::initializer_list<std::string_view> expected,
    std::string_view label) {
    if (!object.is_object()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " must be an object");
    }
    std::set<std::string> expected_keys;
    for (const auto key : expected) {
        expected_keys.emplace(key);
        if (!object.contains(std::string(key))) {
            reject(
                CatalogLoadStatus::Missing,
                std::string(label) + " is missing " + std::string(key));
        }
    }
    for (const auto& [key, unused] : object.items()) {
        static_cast<void>(unused);
        if (expected_keys.find(key) == expected_keys.end()) {
            reject(
                CatalogLoadStatus::Malformed,
                std::string(label) + " has unknown field " + key);
        }
    }
}

std::string require_string(const json& value, std::string_view label) {
    if (!value.is_string()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " must be a string");
    }
    const auto result = value.get<std::string>();
    if (result.empty() || result.size() > 512 ||
        std::any_of(result.begin(), result.end(), [](unsigned char character) {
            return character < 0x20;
        })) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " is invalid");
    }
    return result;
}

std::string required_string(
    const json& object,
    std::string_view key,
    std::string_view label) {
    return require_string(required(object, key, label), label);
}

void require_literal(
    const json& object,
    std::string_view key,
    std::string_view expected,
    std::string_view label) {
    if (required_string(object, key, label) != expected) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " has an invalid value");
    }
}

bool is_lower_hex(std::string_view value, std::size_t size) {
    return value.size() == size &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

template <typename T>
std::vector<T> sorted_unique(std::vector<T> values, std::string_view label) {
    std::sort(values.begin(), values.end());
    if (std::adjacent_find(values.begin(), values.end()) != values.end()) {
        reject(CatalogLoadStatus::Duplicate, std::string(label) + " contains duplicates");
    }
    return values;
}

OperationTemplate parse_operation_template(const json& value, std::string_view label) {
    const auto decoded = decode_operation_template(require_string(value, label));
    if (!decoded.is_known()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " is unknown");
    }
    return *decoded.known_value();
}

OperationKind parse_operation_kind(const json& value, std::string_view label) {
    const auto decoded = decode_operation_kind(require_string(value, label));
    if (!decoded.is_known()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " is unknown");
    }
    return *decoded.known_value();
}

ConstraintKind parse_constraint(const json& value, std::string_view label) {
    const auto decoded = decode_constraint_kind(require_string(value, label));
    if (!decoded.is_known()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " is unknown");
    }
    return *decoded.known_value();
}

CapabilityLevel parse_capability(const json& value, std::string_view label) {
    const auto decoded = decode_capability_level(require_string(value, label));
    if (!decoded.is_known()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " is unknown");
    }
    return *decoded.known_value();
}

DeliveryState parse_delivery(const json& value, std::string_view label) {
    const auto decoded = decode_delivery_state(require_string(value, label));
    if (!decoded.is_known()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " is unknown");
    }
    return *decoded.known_value();
}

int capability_rank(CapabilityLevel value) {
    switch (value) {
    case CapabilityLevel::Unsupported:
        return 0;
    case CapabilityLevel::FallbackOnly:
        return 1;
    case CapabilityLevel::Modeled:
        return 2;
    case CapabilityLevel::Validated:
        return 3;
    }
    return -1;
}

void require_within_evidence_ceiling(
    CapabilityLevel capability_level,
    CapabilityLevel evidence_ceiling,
    DeliveryState delivery_state,
    std::string_view label) {
    const auto capability = capability_rank(capability_level);
    const auto ceiling = capability_rank(evidence_ceiling);
    if (capability < 0 || ceiling < 0 || capability > ceiling) {
        reject(
            CatalogLoadStatus::Malformed,
            std::string(label) + " exceeds its evidence ceiling");
    }
    if (delivery_state == DeliveryState::Absent &&
        capability_level != CapabilityLevel::Unsupported) {
        reject(
            CatalogLoadStatus::Malformed,
            std::string(label) + " is unavailable while delivery is absent");
    }
}

PromotionUnitKind parse_unit_kind(const json& value, std::string_view label) {
    const auto decoded = decode_promotion_unit_kind(require_string(value, label));
    if (!decoded.is_known()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " is unknown");
    }
    return *decoded.known_value();
}

PromotionUnitId parse_unit_id(const json& value, std::string_view label) {
    const auto decoded = decode_promotion_unit_id(require_string(value, label));
    if (!decoded.is_known()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " is unknown");
    }
    return *decoded.known_value();
}

FallbackId parse_fallback_id(const json& value, std::string_view label) {
    const auto decoded = decode_fallback_id(require_string(value, label));
    if (!decoded.is_known()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " is unknown");
    }
    return *decoded.known_value();
}

bool template_accepts(OperationTemplate operation_template, OperationKind operation_kind) {
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
               operation_kind == OperationKind::ArtifactScopeRecoveryCleanup;
    case OperationTemplate::Unl:
        return operation_kind == OperationKind::ExplicitUnload ||
               operation_kind == OperationKind::ForceUnload;
    case OperationTemplate::Pin:
        return operation_kind == OperationKind::SavedPinMutation ||
               operation_kind == OperationKind::RuntimePinMutation ||
               operation_kind == OperationKind::LegacyPinBatch ||
               operation_kind == OperationKind::ResidentStateRecoveryCleanup;
    }
    return false;
}

std::vector<std::string> parse_string_array(
    const json& value,
    std::string_view label,
    bool allow_empty = false) {
    if (!value.is_array() || (!allow_empty && value.empty())) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " must be an array");
    }
    std::vector<std::string> result;
    result.reserve(value.size());
    for (const auto& member : value) {
        result.push_back(require_string(member, label));
    }
    return sorted_unique(std::move(result), label);
}

std::vector<OperationKind> parse_operation_kinds(
    const json& value,
    OperationTemplate operation_template,
    std::string_view label) {
    if (!value.is_array() || value.empty()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " must be an array");
    }
    std::vector<OperationKind> result;
    result.reserve(value.size());
    for (const auto& member : value) {
        const auto operation_kind = parse_operation_kind(member, label);
        if (!template_accepts(operation_template, operation_kind)) {
            reject(
                CatalogLoadStatus::Malformed,
                std::string(label) + " is incompatible with its operation template");
        }
        result.push_back(operation_kind);
    }
    return sorted_unique(std::move(result), label);
}

std::vector<ConstraintKind> parse_constraints(const json& value, std::string_view label) {
    if (!value.is_array() || value.empty()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " must be an array");
    }
    std::vector<ConstraintKind> result;
    result.reserve(value.size());
    for (const auto& member : value) {
        result.push_back(parse_constraint(member, label));
    }
    return sorted_unique(std::move(result), label);
}

std::optional<std::vector<ConstraintKind>> normalized_constraints(
    const std::vector<ConstraintKind>& values) {
    if (values.empty()) {
        return std::nullopt;
    }
    std::vector<ConstraintKind> result;
    result.reserve(values.size());
    for (const auto value : values) {
        if (wire_name(value).empty()) {
            return std::nullopt;
        }
        result.push_back(value);
    }
    std::sort(result.begin(), result.end());
    if (std::adjacent_find(result.begin(), result.end()) != result.end()) {
        return std::nullopt;
    }
    return result;
}

std::map<std::string, std::string> parse_material_profiles(
    const json& value,
    std::string_view label,
    bool require_all) {
    if (!value.is_object()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " must be an object");
    }
    std::map<std::string, std::string> result;
    for (const auto& [key, member] : value.items()) {
        if (std::find(material_profile_keys.begin(), material_profile_keys.end(), key) ==
            material_profile_keys.end()) {
            reject(
                CatalogLoadStatus::Malformed,
                std::string(label) + " has unknown field " + key);
        }
        result.emplace(key, require_string(member, label));
    }
    if (require_all && result.size() != material_profile_keys.size()) {
        reject(CatalogLoadStatus::Missing, std::string(label) + " is incomplete");
    }
    return result;
}

std::map<std::string, std::string> parse_exact_material_profiles(
    const json& value,
    std::string_view label) {
    std::map<std::string, std::string> result;
    for (const auto key : material_profile_keys) {
        result.emplace(
            std::string(key),
            required_string(value, key, label));
    }
    return result;
}

std::map<std::string, FallbackId> parse_fallbacks(
    const json& value,
    const FallbackRegistry& fallback_registry,
    OperationTemplate operation_template,
    std::string_view label) {
    if (!value.is_object() || value.empty()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " must be an object");
    }
    std::map<std::string, FallbackId> result;
    for (const auto& [condition, raw_id] : value.items()) {
        if (condition.empty()) {
            reject(CatalogLoadStatus::Malformed, std::string(label) + " has an empty condition");
        }
        auto fallback_id = parse_fallback_id(raw_id, label);
        const auto registry_entry = fallback_registry.find(fallback_id.token());
        if (registry_entry == fallback_registry.end() ||
            !std::binary_search(
                registry_entry->second.begin(),
                registry_entry->second.end(),
                operation_template)) {
            reject(CatalogLoadStatus::Malformed, std::string(label) + " is not closed");
        }
        result.emplace(condition, std::move(fallback_id));
    }
    return result;
}

struct RuntimePredicate {
    std::string base_variant;
    std::string platform;
    std::string backend_channel;
    std::vector<std::string> model_types;
    OperationTemplate operation_template;
    std::vector<OperationKind> operation_kinds;
    std::vector<ConstraintKind> constraints;
    std::string recovery;
    std::map<std::string, std::string> material_profiles;
};

struct RuntimeUnit {
    PromotionUnitId id;
    RuntimePredicate predicate;
    CapabilityLevel capability_level;
    CapabilityLevel evidence_ceiling;
    DeliveryState delivery_state;
    std::map<std::string, FallbackId> fallbacks;
    std::vector<PromotionUnitId> compatibility_contract_ids;
};

struct CompatibilityCase {
    std::string platform;
    std::string coexist_by_type_variant;
    std::string exclusive_variant;
};

struct CompatibilityUnit {
    PromotionUnitId id;
    std::vector<CompatibilityCase> cases;
    std::vector<std::string> directions;
    std::vector<std::string> incumbent_states;
    std::vector<ConstraintKind> constraints;
    CapabilityLevel capability_level;
    CapabilityLevel evidence_ceiling;
    DeliveryState delivery_state;
    std::map<std::string, FallbackId> fallbacks;
};

struct ParsedCatalog {
    std::string source_support_baseline;
    std::string selection_registry_sha256;
    std::vector<RuntimeUnit> runtime_units;
    std::vector<CompatibilityUnit> compatibility_units;
};

FallbackRegistry parse_fallback_registry(const json& value) {
    if (!value.is_array() || value.size() != expected_fallback_count) {
        reject(CatalogLoadStatus::Missing, "catalog fallback roster is incomplete");
    }
    FallbackRegistry registry;
    for (const auto& row : value) {
        require_exact_keys(row, {"effect", "guard", "id", "operations"}, "fallback");
        auto id = parse_fallback_id(required(row, "id", "fallback"), "fallback.id");
        if (registry.find(id.token()) != registry.end()) {
            reject(CatalogLoadStatus::Duplicate, "catalog has a duplicate fallback ID");
        }
        require_string(required(row, "effect", "fallback"), "fallback.effect");
        require_string(required(row, "guard", "fallback"), "fallback.guard");
        const auto& raw_operations = required(row, "operations", "fallback");
        if (!raw_operations.is_array() || raw_operations.empty()) {
            reject(CatalogLoadStatus::Malformed, "fallback operations must be an array");
        }
        std::vector<OperationTemplate> operations;
        for (const auto& operation : raw_operations) {
            operations.push_back(
                parse_operation_template(operation, "fallback operation"));
        }
        registry.emplace(
            id.token(),
            sorted_unique(std::move(operations), "fallback operations"));
    }
    return registry;
}

std::vector<PromotionUnitId> parse_compatibility_references(
    const json& value,
    std::string_view label) {
    if (!value.is_array()) {
        reject(CatalogLoadStatus::Malformed, std::string(label) + " must be an array");
    }
    std::vector<PromotionUnitId> result;
    std::set<std::string> seen;
    for (const auto& member : value) {
        auto id = parse_unit_id(member, label);
        if (!seen.emplace(id.token()).second) {
            reject(CatalogLoadStatus::Duplicate, std::string(label) + " contains duplicates");
        }
        result.push_back(std::move(id));
    }
    return result;
}

RuntimeUnit parse_exact_cell(
    const json& contract,
    PromotionUnitId id,
    CapabilityLevel capability_level,
    DeliveryState delivery_state,
    const FallbackRegistry& fallback_registry) {
    require_exact_keys(
        contract,
        {"base_variant",
         "campaign_gate_set",
         "capability_level",
         "cell_id",
         "constraints",
         "delivery_state",
         "evidence_ceiling",
         "fallbacks",
         "match",
         "operation_leaf",
         "operation_template",
         "promotion_target",
         "runtime_bindings",
         "scope"},
        "exact cell contract");
    if (required_string(contract, "cell_id", "exact cell") != id.token() ||
        parse_capability(required(contract, "capability_level", "exact cell"), "exact cell") !=
            capability_level ||
        parse_delivery(required(contract, "delivery_state", "exact cell"), "exact cell") !=
            delivery_state) {
        reject(CatalogLoadStatus::Malformed, "exact cell identity or state disagrees");
    }

    const auto operation_template = parse_operation_template(
        required(contract, "operation_template", "exact cell"),
        "exact cell operation_template");
    const auto operation_kind = parse_operation_kind(
        required(contract, "operation_leaf", "exact cell"),
        "exact cell operation_leaf");
    if (!template_accepts(operation_template, operation_kind)) {
        reject(CatalogLoadStatus::Malformed, "exact cell operation is incoherent");
    }

    const auto& match = required(contract, "match", "exact cell");
    require_exact_keys(
        match,
        {"backend_channel",
         "configuration_profile",
         "hardware_profile",
         "model_types",
         "observation_contract",
         "platform",
         "predictor_rule",
         "recovery",
         "workload_profile"},
        "exact cell match");
    const auto profiles =
        parse_exact_material_profiles(match, "exact cell match");

    require_string(required(contract, "campaign_gate_set", "exact cell"), "campaign gate set");
    const auto evidence_ceiling = parse_capability(
        required(contract, "evidence_ceiling", "exact cell"),
        "evidence ceiling");
    require_within_evidence_ceiling(
        capability_level,
        evidence_ceiling,
        delivery_state,
        "exact cell capability");
    require_string(required(contract, "promotion_target", "exact cell"), "promotion target");
    parse_string_array(
        required(contract, "runtime_bindings", "exact cell"),
        "runtime bindings");
    require_string(required(contract, "scope", "exact cell"), "exact cell scope");

    RuntimePredicate predicate{
        required_string(contract, "base_variant", "exact cell"),
        required_string(match, "platform", "exact cell match"),
        required_string(match, "backend_channel", "exact cell match"),
        parse_string_array(
            required(match, "model_types", "exact cell match"),
            "exact cell model_types"),
        operation_template,
        {operation_kind},
        parse_constraints(
            required(contract, "constraints", "exact cell"),
            "exact cell constraints"),
        required_string(match, "recovery", "exact cell match"),
        profiles,
    };
    return RuntimeUnit{
        std::move(id),
        std::move(predicate),
        capability_level,
        evidence_ceiling,
        delivery_state,
        parse_fallbacks(
            required(contract, "fallbacks", "exact cell"),
            fallback_registry,
            operation_template,
            "exact cell fallbacks"),
        {},
    };
}

RuntimeUnit parse_later_runtime(
    const json& contract,
    PromotionUnitId id,
    CapabilityLevel capability_level,
    DeliveryState delivery_state,
    const FallbackRegistry& fallback_registry) {
    require_exact_keys(
        contract,
        {"compatibility_contracts",
         "constraints",
         "delivery_gate",
         "evidence_ceiling",
         "evidence_gate_set",
         "expected_roots",
         "fallbacks",
         "initial_state",
         "issue_id",
         "material_profiles",
         "recovery",
         "selector",
         "unit_id"},
        "later runtime contract");
    if (required_string(contract, "unit_id", "later runtime") != id.token()) {
        reject(CatalogLoadStatus::Malformed, "later runtime identity disagrees");
    }
    const auto& state = required(contract, "initial_state", "later runtime");
    require_exact_keys(state, {"capability_level", "delivery_state"}, "initial state");
    if (parse_capability(required(state, "capability_level", "initial state"), "initial state") !=
            capability_level ||
        parse_delivery(required(state, "delivery_state", "initial state"), "initial state") !=
            delivery_state) {
        reject(CatalogLoadStatus::Malformed, "later runtime state disagrees");
    }
    if (required_string(contract, "delivery_gate", "later runtime") !=
        "release_verified:" + id.token()) {
        reject(CatalogLoadStatus::Malformed, "later runtime delivery gate disagrees");
    }
    const auto& issue_id = required(contract, "issue_id", "later runtime");
    if (!issue_id.is_number_integer() || issue_id.get<long long>() <= 0) {
        reject(CatalogLoadStatus::Malformed, "later runtime issue ID is invalid");
    }
    const auto& roots = required(contract, "expected_roots", "later runtime");
    require_exact_keys(roots, {"implementation", "outputs", "tests"}, "expected roots");
    for (const auto key : {"implementation", "outputs", "tests"}) {
        required_string(roots, key, "expected roots");
    }
    const auto evidence_ceiling = parse_capability(
        required(contract, "evidence_ceiling", "later runtime"),
        "evidence ceiling");
    require_within_evidence_ceiling(
        capability_level,
        evidence_ceiling,
        delivery_state,
        "later runtime capability");
    required_string(contract, "evidence_gate_set", "later runtime");

    const auto& selector = required(contract, "selector", "later runtime");
    require_exact_keys(
        selector,
        {"backend_channel",
         "base_variant",
         "model_type",
         "operation_leaves",
         "operation_template",
         "platform"},
        "later runtime selector");
    const auto operation_template = parse_operation_template(
        required(selector, "operation_template", "later runtime selector"),
        "later runtime operation_template");

    RuntimePredicate predicate{
        required_string(selector, "base_variant", "later runtime selector"),
        required_string(selector, "platform", "later runtime selector"),
        required_string(selector, "backend_channel", "later runtime selector"),
        {required_string(selector, "model_type", "later runtime selector")},
        operation_template,
        parse_operation_kinds(
            required(selector, "operation_leaves", "later runtime selector"),
            operation_template,
            "later runtime operation_leaves"),
        parse_constraints(
            required(contract, "constraints", "later runtime"),
            "later runtime constraints"),
        required_string(contract, "recovery", "later runtime"),
        parse_material_profiles(
            required(contract, "material_profiles", "later runtime"),
            "later runtime material_profiles",
            false),
    };
    return RuntimeUnit{
        std::move(id),
        std::move(predicate),
        capability_level,
        evidence_ceiling,
        delivery_state,
        parse_fallbacks(
            required(contract, "fallbacks", "later runtime"),
            fallback_registry,
            operation_template,
            "later runtime fallbacks"),
        parse_compatibility_references(
            required(contract, "compatibility_contracts", "later runtime"),
            "later runtime compatibility_contracts"),
    };
}

CompatibilityUnit parse_compatibility_contract(
    const json& contract,
    PromotionUnitId id,
    CapabilityLevel capability_level,
    DeliveryState delivery_state,
    const FallbackRegistry& fallback_registry) {
    require_exact_keys(
        contract,
        {"campaign_gate_set",
         "capability_level",
         "contract_id",
         "delivery_state",
         "directions",
         "evidence_ceiling",
         "evidence_mode",
         "fallbacks",
         "incumbent_states",
         "model_type_coverage",
         "operation_leaf",
         "operation_template",
         "platform_cases",
         "promotion_target",
         "relation_constraints",
         "runtime_authority",
         "scope",
         "suite_set"},
        "compatibility contract");
    if (required_string(contract, "contract_id", "compatibility contract") != id.token() ||
        parse_capability(
            required(contract, "capability_level", "compatibility contract"),
            "compatibility contract") != capability_level ||
        parse_delivery(
            required(contract, "delivery_state", "compatibility contract"),
            "compatibility contract") != delivery_state) {
        reject(CatalogLoadStatus::Malformed, "compatibility identity or state disagrees");
    }
    const auto operation_template = parse_operation_template(
        required(contract, "operation_template", "compatibility contract"),
        "compatibility operation_template");
    const auto operation_kind = parse_operation_kind(
        required(contract, "operation_leaf", "compatibility contract"),
        "compatibility operation_leaf");
    if (operation_template != OperationTemplate::Npc ||
        !template_accepts(operation_template, operation_kind)) {
        reject(CatalogLoadStatus::Malformed, "compatibility operation is incoherent");
    }
    require_literal(
        contract,
        "runtime_authority",
        "none",
        "compatibility runtime_authority");
    require_literal(
        contract,
        "evidence_mode",
        "synthetic_only",
        "compatibility evidence_mode");
    require_literal(
        contract,
        "model_type_coverage",
        "all_declared_by_participant",
        "compatibility model_type_coverage");
    const auto evidence_ceiling = parse_capability(
        required(contract, "evidence_ceiling", "compatibility contract"),
        "compatibility evidence ceiling");
    require_within_evidence_ceiling(
        capability_level,
        evidence_ceiling,
        delivery_state,
        "compatibility capability");
    for (const auto key : {
             "campaign_gate_set",
             "promotion_target",
             "scope",
             "suite_set",
         }) {
        required_string(contract, key, "compatibility contract");
    }

    const auto& raw_cases = required(contract, "platform_cases", "compatibility contract");
    if (!raw_cases.is_array() || raw_cases.empty()) {
        reject(CatalogLoadStatus::Malformed, "compatibility platform_cases must be an array");
    }
    std::vector<CompatibilityCase> cases;
    std::set<std::array<std::string, 3>> seen_cases;
    for (const auto& raw_case : raw_cases) {
        require_exact_keys(
            raw_case,
            {"coexist_by_type_variant", "exclusive_variant", "platform"},
            "compatibility platform case");
        CompatibilityCase parsed{
            required_string(raw_case, "platform", "compatibility platform case"),
            required_string(
                raw_case,
                "coexist_by_type_variant",
                "compatibility platform case"),
            required_string(raw_case, "exclusive_variant", "compatibility platform case"),
        };
        if (!seen_cases.emplace(
                 std::array<std::string, 3>{
                     parsed.platform,
                     parsed.coexist_by_type_variant,
                     parsed.exclusive_variant,
                 })
                 .second) {
            reject(CatalogLoadStatus::Duplicate, "compatibility platform case is duplicated");
        }
        cases.push_back(std::move(parsed));
    }

    const auto directions = parse_string_array(
        required(contract, "directions", "compatibility contract"),
        "compatibility directions");
    if (directions != std::vector<std::string>{
                          "coexist_by_type_incoming",
                          "exclusive_incoming",
                      }) {
        reject(CatalogLoadStatus::Malformed, "compatibility directions are unknown");
    }
    const auto incumbent_states = parse_string_array(
        required(contract, "incumbent_states", "compatibility contract"),
        "compatibility incumbent_states");
    if (incumbent_states != std::vector<std::string>{
                                "in_use",
                                "pinned",
                                "unpinned_idle",
                            }) {
        reject(CatalogLoadStatus::Malformed, "compatibility incumbent states are unknown");
    }

    return CompatibilityUnit{
        std::move(id),
        std::move(cases),
        directions,
        incumbent_states,
        parse_constraints(
            required(contract, "relation_constraints", "compatibility contract"),
            "compatibility relation_constraints"),
        capability_level,
        evidence_ceiling,
        delivery_state,
        parse_fallbacks(
            required(contract, "fallbacks", "compatibility contract"),
            fallback_registry,
            operation_template,
            "compatibility fallbacks"),
    };
}

bool strings_intersect(
    const std::vector<std::string>& left,
    const std::vector<std::string>& right) {
    return std::any_of(left.begin(), left.end(), [&](const auto& value) {
        return std::binary_search(right.begin(), right.end(), value);
    });
}

bool operations_intersect(
    const std::vector<OperationKind>& left,
    const std::vector<OperationKind>& right) {
    return std::any_of(left.begin(), left.end(), [&](const auto value) {
        return std::binary_search(right.begin(), right.end(), value);
    });
}

bool profiles_compatible(
    const std::map<std::string, std::string>& left,
    const std::map<std::string, std::string>& right) {
    for (const auto& [key, value] : left) {
        const auto found = right.find(key);
        if (found != right.end() && found->second != value) {
            return false;
        }
    }
    return true;
}

bool predicates_overlap(const RuntimePredicate& left, const RuntimePredicate& right) {
    return left.base_variant == right.base_variant && left.platform == right.platform &&
           left.backend_channel == right.backend_channel &&
           left.operation_template == right.operation_template &&
           left.constraints == right.constraints && left.recovery == right.recovery &&
           strings_intersect(left.model_types, right.model_types) &&
           operations_intersect(left.operation_kinds, right.operation_kinds) &&
           profiles_compatible(left.material_profiles, right.material_profiles);
}

template <typename T>
bool includes_all(const std::vector<T>& broader, const std::vector<T>& narrower) {
    return std::includes(
        broader.begin(),
        broader.end(),
        narrower.begin(),
        narrower.end());
}

bool profiles_include_all(
    const std::map<std::string, std::string>& broader,
    const std::map<std::string, std::string>& narrower) {
    return std::all_of(broader.begin(), broader.end(), [&](const auto& member) {
        const auto found = narrower.find(member.first);
        return found != narrower.end() && found->second == member.second;
    });
}

bool predicate_subsumes(const RuntimePredicate& broader, const RuntimePredicate& narrower) {
    return predicates_overlap(broader, narrower) &&
           includes_all(broader.model_types, narrower.model_types) &&
           includes_all(broader.operation_kinds, narrower.operation_kinds) &&
           profiles_include_all(broader.material_profiles, narrower.material_profiles);
}

void validate_runtime_disjointness(const std::vector<RuntimeUnit>& units) {
    for (std::size_t left_index = 0; left_index < units.size(); ++left_index) {
        for (std::size_t right_index = left_index + 1; right_index < units.size(); ++right_index) {
            const auto& left = units[left_index].predicate;
            const auto& right = units[right_index].predicate;
            if (!predicates_overlap(left, right)) {
                continue;
            }
            const bool left_subsumes = predicate_subsumes(left, right);
            const bool right_subsumes = predicate_subsumes(right, left);
            if (!left_subsumes && !right_subsumes) {
                reject(
                    CatalogLoadStatus::PrecedenceIncomparable,
                    "catalog contains precedence-incomparable runtime predicates");
            }
            reject(
                CatalogLoadStatus::Ambiguous,
                "catalog contains overlapping runtime predicates without declared precedence");
        }
    }
}

ParsedCatalog parse_catalog(std::string_view bytes) {
    bool duplicate_key = false;
    std::string duplicate_name;
    std::vector<std::unordered_set<std::string>> object_keys;
    const auto callback = [&](int, json::parse_event_t event, json& parsed) {
        if (event == json::parse_event_t::object_start) {
            object_keys.emplace_back();
        } else if (event == json::parse_event_t::key) {
            const auto key = parsed.get<std::string>();
            if (!object_keys.back().emplace(key).second && !duplicate_key) {
                duplicate_key = true;
                duplicate_name = key;
            }
        } else if (event == json::parse_event_t::object_end) {
            object_keys.pop_back();
        }
        return true;
    };

    json document;
    try {
        document = json::parse(bytes.begin(), bytes.end(), callback, true, false);
    } catch (const json::parse_error& error) {
        reject(CatalogLoadStatus::Malformed, std::string("catalog JSON is invalid: ") + error.what());
    }
    if (duplicate_key) {
        reject(CatalogLoadStatus::Duplicate, "catalog has duplicate JSON key " + duplicate_name);
    }

    require_exact_keys(
        document,
        {"schema",
         "generator_version",
         "source_support_baseline",
         "selection_registry_sha256",
         "promotion_units",
         "fallbacks",
         "contract_registry"},
        "catalog");
    require_literal(document, "schema", "residency.profiles/1.0", "catalog schema");
    const auto& generator_version = required(document, "generator_version", "catalog");
    if (!generator_version.is_number_integer() || generator_version.get<long long>() != 1) {
        reject(CatalogLoadStatus::Malformed, "catalog generator version is unsupported");
    }
    const auto source_support_baseline =
        required_string(document, "source_support_baseline", "catalog");
    if (!is_lower_hex(source_support_baseline, 40)) {
        reject(CatalogLoadStatus::Malformed, "catalog source-support baseline is invalid");
    }
    const auto selection_registry_sha256 =
        required_string(document, "selection_registry_sha256", "catalog");
    if (!is_lower_hex(selection_registry_sha256, 64)) {
        reject(CatalogLoadStatus::Malformed, "catalog selection-registry digest is invalid");
    }
    const auto& registry = required(document, "contract_registry", "catalog");
    if (!registry.is_object() ||
        required_string(registry, "schema", "contract_registry") !=
            "residency.explanation/1.0") {
        reject(CatalogLoadStatus::Malformed, "catalog contract registry is invalid");
    }

    const auto fallback_registry =
        parse_fallback_registry(required(document, "fallbacks", "catalog"));
    const auto& raw_units = required(document, "promotion_units", "catalog");
    if (!raw_units.is_array() || raw_units.size() != expected_promotion_unit_count) {
        reject(CatalogLoadStatus::Missing, "catalog promotion roster is incomplete");
    }

    ParsedCatalog result{source_support_baseline, selection_registry_sha256, {}, {}};
    std::set<std::string> unit_ids;
    for (const auto& unit : raw_units) {
        require_exact_keys(
            unit,
            {"id", "unit_kind", "capability_level", "delivery_state", "contract"},
            "promotion unit");
        auto id = parse_unit_id(required(unit, "id", "promotion unit"), "promotion unit.id");
        if (!unit_ids.emplace(id.token()).second) {
            reject(CatalogLoadStatus::Duplicate, "catalog has a duplicate promotion-unit ID");
        }
        const auto unit_kind = parse_unit_kind(
            required(unit, "unit_kind", "promotion unit"),
            "promotion unit.unit_kind");
        if (promotion_unit_kind(id) != unit_kind) {
            reject(
                CatalogLoadStatus::Malformed,
                "promotion-unit identity is registered under a different kind");
        }
        const auto capability_level = parse_capability(
            required(unit, "capability_level", "promotion unit"),
            "promotion unit.capability_level");
        const auto delivery_state = parse_delivery(
            required(unit, "delivery_state", "promotion unit"),
            "promotion unit.delivery_state");
        const auto& contract = required(unit, "contract", "promotion unit");
        switch (unit_kind) {
        case PromotionUnitKind::ExactCell:
            result.runtime_units.push_back(parse_exact_cell(
                contract,
                std::move(id),
                capability_level,
                delivery_state,
                fallback_registry));
            break;
        case PromotionUnitKind::CompatibilityContract:
            result.compatibility_units.push_back(parse_compatibility_contract(
                contract,
                std::move(id),
                capability_level,
                delivery_state,
                fallback_registry));
            break;
        case PromotionUnitKind::LaterRuntime:
            result.runtime_units.push_back(parse_later_runtime(
                contract,
                std::move(id),
                capability_level,
                delivery_state,
                fallback_registry));
            break;
        }
    }

    std::set<std::string> compatibility_ids;
    for (const auto& contract : result.compatibility_units) {
        compatibility_ids.emplace(contract.id.token());
    }
    for (const auto& unit : result.runtime_units) {
        for (const auto& reference : unit.compatibility_contract_ids) {
            if (compatibility_ids.find(reference.token()) == compatibility_ids.end()) {
                reject(CatalogLoadStatus::Malformed, "runtime unit references a non-compatibility unit");
            }
        }
    }
    validate_runtime_disjointness(result.runtime_units);
    return result;
}

using ParseResult = std::variant<ParsedCatalog, CatalogValidationResult>;

ParseResult try_parse_catalog(std::string_view bytes) {
    try {
        return parse_catalog(bytes);
    } catch (const CatalogFailure& error) {
        return CatalogValidationResult{error.status(), bounded_diagnostic(error.what())};
    } catch (const json::exception& error) {
        return CatalogValidationResult{
            CatalogLoadStatus::Malformed,
            bounded_diagnostic(error.what()),
        };
    } catch (const std::exception& error) {
        return CatalogValidationResult{
            CatalogLoadStatus::Malformed,
            bounded_diagnostic(error.what()),
        };
    }
}

std::optional<std::string> sha256_hex(std::string_view bytes) {
    const auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) {
        return std::nullopt;
    }
    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    std::array<unsigned char, 32> digest{};
    const bool failed =
        mbedtls_md_setup(&context, info, 0) != 0 ||
        mbedtls_md_starts(&context) != 0 ||
        mbedtls_md_update(
            &context,
            reinterpret_cast<const unsigned char*>(bytes.data()),
            bytes.size()) != 0 ||
        mbedtls_md_finish(&context, digest.data()) != 0;
    mbedtls_md_free(&context);
    if (failed) {
        return std::nullopt;
    }
    constexpr std::string_view hexadecimal = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(hexadecimal[(byte >> 4) & 0x0f]);
        result.push_back(hexadecimal[byte & 0x0f]);
    }
    return result;
}

bool selector_profiles_valid(const std::map<std::string, std::string>& profiles) {
    return std::all_of(profiles.begin(), profiles.end(), [](const auto& member) {
        return !member.second.empty() &&
               std::find(
                   material_profile_keys.begin(),
                   material_profile_keys.end(),
                   member.first) != material_profile_keys.end();
    });
}

bool runtime_selector_matches(
    const RuntimePredicate& predicate,
    const RuntimeCatalogSelector& selector,
    const std::vector<ConstraintKind>& constraints) {
    if (predicate.base_variant != selector.base_variant ||
        predicate.platform != selector.platform ||
        predicate.backend_channel != selector.backend_channel ||
        predicate.operation_template != selector.operation_template ||
        predicate.constraints != constraints || predicate.recovery != selector.recovery ||
        !std::binary_search(
            predicate.model_types.begin(),
            predicate.model_types.end(),
            selector.model_type) ||
        !std::binary_search(
            predicate.operation_kinds.begin(),
            predicate.operation_kinds.end(),
            selector.operation_kind)) {
        return false;
    }
    return std::all_of(
        predicate.material_profiles.begin(),
        predicate.material_profiles.end(),
        [&](const auto& member) {
            const auto found = selector.material_profiles.find(member.first);
            return found != selector.material_profiles.end() && found->second == member.second;
        });
}

CatalogSelection selection_for(const RuntimeUnit& unit) {
    auto selection = catalog_internal::make_catalog_selection(
        unit.id,
        unit.capability_level,
        unit.evidence_ceiling,
        unit.delivery_state);
    selection.fallbacks = unit.fallbacks;
    selection.constraints = unit.predicate.constraints;
    selection.compatibility_contract_ids = unit.compatibility_contract_ids;
    return selection;
}

CatalogSelection selection_for(const CompatibilityUnit& unit) {
    auto selection = catalog_internal::make_catalog_selection(
        unit.id,
        unit.capability_level,
        unit.evidence_ceiling,
        unit.delivery_state);
    selection.fallbacks = unit.fallbacks;
    selection.constraints = unit.constraints;
    return selection;
}

}

struct CatalogSnapshot::State {
    std::string catalog_sha256;
    std::string source_support_baseline;
    std::string selection_registry_sha256;
    std::vector<RuntimeUnit> runtime_units;
    std::vector<CompatibilityUnit> compatibility_units;
};

CatalogSnapshot::CatalogSnapshot(std::shared_ptr<const State> state)
    : state_(std::move(state)) {}

std::string_view CatalogSnapshot::catalog_sha256() const noexcept {
    return state_->catalog_sha256;
}

std::string_view CatalogSnapshot::source_support_baseline() const noexcept {
    return state_->source_support_baseline;
}

std::string_view CatalogSnapshot::selection_registry_sha256() const noexcept {
    return state_->selection_registry_sha256;
}

CatalogSelection CatalogSnapshot::resolve(const RuntimeCatalogSelector& selector) const {
    if (selector.source_support_baseline != state_->source_support_baseline ||
        selector.base_variant.empty() || selector.platform.empty() ||
        selector.backend_channel.empty() || selector.model_type.empty() ||
        selector.recovery.empty() || wire_name(selector.operation_template).empty() ||
        wire_name(selector.operation_kind).empty() ||
        !template_accepts(selector.operation_template, selector.operation_kind) ||
        !selector_profiles_valid(selector.material_profiles)) {
        return {};
    }
    const auto constraints = normalized_constraints(selector.constraints);
    if (!constraints.has_value()) {
        return {};
    }
    const RuntimeUnit* match = nullptr;
    for (const auto& unit : state_->runtime_units) {
        if (!runtime_selector_matches(unit.predicate, selector, *constraints)) {
            continue;
        }
        if (match != nullptr) {
            return {};
        }
        match = &unit;
    }
    return match == nullptr ? CatalogSelection{} : selection_for(*match);
}

CatalogSelection CatalogSnapshot::resolve_compatibility(
    const CompatibilityCatalogSelector& selector) const {
    if (selector.source_support_baseline != state_->source_support_baseline ||
        selector.platform.empty() || selector.coexist_by_type_variant.empty() ||
        selector.exclusive_variant.empty() || selector.direction.empty() ||
        selector.incumbent_state.empty()) {
        return {};
    }
    const auto constraints = normalized_constraints(selector.constraints);
    if (!constraints.has_value()) {
        return {};
    }
    const CompatibilityUnit* match = nullptr;
    for (const auto& unit : state_->compatibility_units) {
        if (unit.constraints != *constraints ||
            !std::binary_search(unit.directions.begin(), unit.directions.end(), selector.direction) ||
            !std::binary_search(
                unit.incumbent_states.begin(),
                unit.incumbent_states.end(),
                selector.incumbent_state)) {
            continue;
        }
        const bool case_matches = std::any_of(
            unit.cases.begin(),
            unit.cases.end(),
            [&](const auto& candidate) {
                return candidate.platform == selector.platform &&
                       candidate.coexist_by_type_variant == selector.coexist_by_type_variant &&
                       candidate.exclusive_variant == selector.exclusive_variant;
            });
        if (!case_matches) {
            continue;
        }
        if (match != nullptr) {
            return {};
        }
        match = &unit;
    }
    return match == nullptr ? CatalogSelection{} : selection_for(*match);
}

CatalogValidationResult validate_catalog_document(std::string_view bytes) {
    const auto parsed = try_parse_catalog(bytes);
    if (const auto* failure = std::get_if<CatalogValidationResult>(&parsed)) {
        return *failure;
    }
    return CatalogValidationResult{CatalogLoadStatus::Accepted, {}};
}

CatalogLoadResult load_packaged_catalog(std::string_view bytes) {
    const auto digest = sha256_hex(bytes);
    if (!digest.has_value() || *digest != packaged_catalog_sha256) {
        return CatalogLoadResult{
            CatalogLoadStatus::DigestMismatch,
            "packaged catalog SHA-256 does not match the generated lock",
            std::nullopt,
        };
    }
    auto parsed = try_parse_catalog(bytes);
    if (const auto* failure = std::get_if<CatalogValidationResult>(&parsed)) {
        return CatalogLoadResult{failure->status, failure->diagnostic, std::nullopt};
    }
    auto catalog = std::get<ParsedCatalog>(std::move(parsed));
    auto state = std::make_shared<CatalogSnapshot::State>();
    state->catalog_sha256 = *digest;
    state->source_support_baseline = std::move(catalog.source_support_baseline);
    state->selection_registry_sha256 = std::move(catalog.selection_registry_sha256);
    state->runtime_units = std::move(catalog.runtime_units);
    state->compatibility_units = std::move(catalog.compatibility_units);
    return CatalogLoadResult{
        CatalogLoadStatus::Accepted,
        {},
        CatalogSnapshot(std::move(state)),
    };
}

}
