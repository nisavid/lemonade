#include "lemon/residency/local_overlay.h"

#include <mbedtls/md.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lemon::residency {

namespace {

using json = nlohmann::json;

constexpr char profiling_input_domain[] =
    "lemonade.residency.local-overlay-profiling-input/v1\0";
constexpr char profiling_phase_attestation_domain[] =
    "lemonade.residency.profiling-phase-attestation/v1\0";
constexpr char local_overlay_domain[] =
    "lemonade.residency.local-overlay-object/v1\0";
constexpr char activation_root_domain[] =
    "lemonade.residency.local-overlay-activation-root/v1\0";
constexpr char selector_identity_domain[] =
    "lemonade.residency.local-overlay-selector/v1\0";
constexpr char method_identity_domain[] =
    "lemonade.residency.local-overlay-method/v1\0";

class OverlayFailure final : public std::runtime_error {
public:
    OverlayFailure(OverlayContractStatus status, std::string message)
        : std::runtime_error(std::move(message)), status_(status) {}

    OverlayContractStatus status() const noexcept { return status_; }

private:
    OverlayContractStatus status_;
};

[[noreturn]] void reject(OverlayContractStatus status, std::string message) {
    throw OverlayFailure(status, std::move(message));
}

std::string bounded_diagnostic(std::string message) {
    if (message.size() > max_local_overlay_diagnostic_bytes) {
        std::size_t boundary = max_local_overlay_diagnostic_bytes;
        while (boundary > 0 &&
               (static_cast<unsigned char>(message[boundary]) & 0xc0) == 0x80) {
            --boundary;
        }
        message.resize(boundary);
    }
    return message;
}

bool schema_is_supported(SchemaVersion schema) noexcept {
    return schema.major == supported_local_overlay_schema.major &&
           schema.minor == supported_local_overlay_schema.minor;
}

bool is_lower_hex(std::string_view value, std::size_t size) noexcept {
    return value.size() == size &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool identifier_is_valid(std::string_view value) noexcept {
    return !value.empty() &&
           value.size() <= max_local_overlay_identifier_bytes &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return character >= 0x21 && character <= 0x7e;
           });
}

void require_identifier(std::string_view value, std::string_view label) {
    if (!identifier_is_valid(value)) {
        reject(OverlayContractStatus::InvalidIdentifier,
               std::string(label) + " is invalid");
    }
}

void require_digest(std::string_view value, std::string_view label) {
    if (!is_lower_hex(value, 64)) {
        reject(OverlayContractStatus::InvalidValue,
               std::string(label) + " is invalid");
    }
}

void require_support_baseline(std::string_view value) {
    if (!is_lower_hex(value, 40)) {
        reject(OverlayContractStatus::InvalidValue,
               "source support baseline is not a lowercase commit digest");
    }
}

bool is_digit(char value) noexcept { return value >= '0' && value <= '9'; }

unsigned parse_two_digits(std::string_view value, std::size_t offset) noexcept {
    return static_cast<unsigned>(value[offset] - '0') * 10U +
           static_cast<unsigned>(value[offset + 1] - '0');
}

unsigned parse_four_digits(std::string_view value,
                           std::size_t offset) noexcept {
    return static_cast<unsigned>(value[offset] - '0') * 1000U +
           static_cast<unsigned>(value[offset + 1] - '0') * 100U +
           static_cast<unsigned>(value[offset + 2] - '0') * 10U +
           static_cast<unsigned>(value[offset + 3] - '0');
}

bool timestamp_is_valid(std::string_view value) noexcept {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
        value[19] != 'Z') {
        return false;
    }
    static constexpr std::array<std::size_t, 14> digit_offsets{
        0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18,
    };
    if (!std::all_of(
            digit_offsets.begin(), digit_offsets.end(),
            [&](std::size_t offset) { return is_digit(value[offset]); })) {
        return false;
    }

    const auto year = parse_four_digits(value, 0);
    const auto month = parse_two_digits(value, 5);
    const auto day = parse_two_digits(value, 8);
    const auto hour = parse_two_digits(value, 11);
    const auto minute = parse_two_digits(value, 14);
    const auto second = parse_two_digits(value, 17);
    if (year == 0 || month == 0 || month > 12 || hour > 23 || minute > 59 ||
        second > 59) {
        return false;
    }
    static constexpr std::array<unsigned, 12> days_per_month{
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    auto maximum_day = days_per_month[month - 1];
    if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0))) {
        maximum_day = 29;
    }
    return day != 0 && day <= maximum_day;
}

void require_timestamp(std::string_view value, std::string_view label) {
    if (!timestamp_is_valid(value)) {
        reject(OverlayContractStatus::InvalidValue,
               std::string(label) + " must be a canonical UTC timestamp");
    }
}

void require_advancing_time(std::string_view earlier, std::string_view later,
                            std::string_view label) {
    require_timestamp(earlier, "earlier timestamp");
    require_timestamp(later, label);
    if (later <= earlier) {
        reject(OverlayContractStatus::InvalidValue,
               std::string(label) + " must be later than its origin");
    }
}

std::string sha256_hex(std::string_view domain, std::string_view payload) {
    const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) {
        reject(OverlayContractStatus::DigestUnavailable,
               "SHA-256 is unavailable");
    }

    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    std::array<unsigned char, 32> digest{};
    const bool failed =
        mbedtls_md_setup(&context, info, 0) != 0 ||
        mbedtls_md_starts(&context) != 0 ||
        mbedtls_md_update(
            &context, reinterpret_cast<const unsigned char *>(domain.data()),
            domain.size()) != 0 ||
        mbedtls_md_update(
            &context, reinterpret_cast<const unsigned char *>(payload.data()),
            payload.size()) != 0 ||
        mbedtls_md_finish(&context, digest.data()) != 0;
    mbedtls_md_free(&context);
    if (failed) {
        reject(OverlayContractStatus::DigestUnavailable, "SHA-256 failed");
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) {
        result.push_back(hex[(byte >> 4) & 0x0f]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

void require_exact_keys(const json &object,
                        std::initializer_list<std::string_view> expected,
                        std::string_view label) {
    if (!object.is_object()) {
        reject(OverlayContractStatus::InvalidValue,
               std::string(label) + " must be an object");
    }
    std::set<std::string> expected_keys;
    for (const auto key : expected) {
        expected_keys.emplace(key);
        if (!object.contains(std::string(key))) {
            reject(OverlayContractStatus::InvalidValue,
                   std::string(label) + " is incomplete");
        }
    }
    for (const auto &[key, value] : object.items()) {
        static_cast<void>(value);
        if (expected_keys.find(key) == expected_keys.end()) {
            reject(OverlayContractStatus::UnknownField,
                   std::string(label) + " has an unknown field");
        }
    }
}

const json &required(const json &object, std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        reject(OverlayContractStatus::InvalidValue,
               "required field is missing");
    }
    return *found;
}

std::string require_string(const json &value, std::string_view label) {
    if (!value.is_string()) {
        reject(OverlayContractStatus::InvalidValue,
               std::string(label) + " must be a string");
    }
    return value.get<std::string>();
}

std::uint64_t require_u64(const json &value, std::string_view label) {
    if (!value.is_number_unsigned()) {
        reject(OverlayContractStatus::InvalidValue,
               std::string(label) + " must be an unsigned integer");
    }
    return value.get<std::uint64_t>();
}

bool require_bool(const json &value, std::string_view label) {
    if (!value.is_boolean()) {
        reject(OverlayContractStatus::InvalidValue,
               std::string(label) + " must be boolean");
    }
    return value.get<bool>();
}

json parse_json(std::string_view bytes) {
    if (bytes.size() > max_local_overlay_input_bytes) {
        reject(OverlayContractStatus::InputTooLarge,
               "candidate document exceeds the input limit");
    }
    if (bytes.find('\0') != std::string_view::npos) {
        reject(OverlayContractStatus::MalformedJson,
               "candidate document contains a NUL byte");
    }

    std::map<int, std::set<std::string>> object_keys;
    const auto callback = [&object_keys](int depth, json::parse_event_t event,
                                         json &parsed) {
        if (event == json::parse_event_t::object_start) {
            object_keys[depth + 1].clear();
        } else if (event == json::parse_event_t::key) {
            auto &keys = object_keys[depth];
            const auto key = parsed.get<std::string>();
            if (!keys.insert(key).second) {
                reject(OverlayContractStatus::Duplicate,
                       "candidate document has a duplicate key");
            }
        } else if (event == json::parse_event_t::object_end) {
            object_keys.erase(depth + 1);
        }
        return true;
    };

    try {
        return json::parse(bytes.begin(), bytes.end(), callback, true, false);
    } catch (const OverlayFailure &) {
        throw;
    } catch (const json::exception &) {
        reject(OverlayContractStatus::MalformedJson,
               "candidate document is malformed JSON");
    }
}

SchemaVersion parse_schema(const json &value) {
    require_exact_keys(value, {"major", "minor"}, "schema");
    const auto major = require_u64(required(value, "major"), "schema major");
    const auto minor = require_u64(required(value, "minor"), "schema minor");
    if (major > std::numeric_limits<std::uint32_t>::max() ||
        minor > std::numeric_limits<std::uint32_t>::max()) {
        reject(OverlayContractStatus::UnsupportedSchema,
               "overlay schema is unsupported");
    }
    const SchemaVersion schema{static_cast<std::uint32_t>(major),
                               static_cast<std::uint32_t>(minor)};
    if (!schema_is_supported(schema)) {
        reject(OverlayContractStatus::UnsupportedSchema,
               "overlay schema is unsupported");
    }
    return schema;
}

json schema_document(SchemaVersion schema) {
    return json{{"major", schema.major}, {"minor", schema.minor}};
}

OperationTemplate parse_operation_template_value(std::string_view wire) {
    const auto decoded = decode_operation_template(wire);
    if (!decoded.is_known()) {
        reject(OverlayContractStatus::UnknownValue,
               "operation template is unknown");
    }
    return *decoded.known_value();
}

OperationKind parse_operation_kind_value(std::string_view wire) {
    const auto decoded = decode_operation_kind(wire);
    if (!decoded.is_known()) {
        reject(OverlayContractStatus::UnknownValue,
               "operation kind is unknown");
    }
    return *decoded.known_value();
}

ConstraintKind parse_constraint_kind(std::string_view wire) {
    const auto decoded = decode_constraint_kind(wire);
    if (!decoded.is_known()) {
        reject(OverlayContractStatus::UnknownValue,
               "constraint kind is unknown");
    }
    return *decoded.known_value();
}

void normalize_catalog_selector(RuntimeCatalogSelector &selector) {
    require_support_baseline(selector.source_support_baseline);
    require_identifier(selector.base_variant, "base variant");
    require_identifier(selector.platform, "platform");
    require_identifier(selector.backend_channel, "backend channel");
    require_identifier(selector.model_type, "model type");
    require_identifier(selector.recovery, "recovery contract");
    if (wire_name(selector.operation_template).empty() ||
        wire_name(selector.operation_kind).empty()) {
        reject(OverlayContractStatus::UnknownValue,
               "catalog selector contains an unknown closed value");
    }
    if (!operation_template_accepts(selector.operation_template,
                                    selector.operation_kind)) {
        reject(OverlayContractStatus::InvalidValue,
               "catalog selector operation template and kind are inconsistent");
    }
    if (selector.constraints.empty() || selector.constraints.size() > 9) {
        reject(OverlayContractStatus::LimitExceeded,
               "catalog selector constraints are empty or exceed the limit");
    }
    for (const auto constraint : selector.constraints) {
        if (wire_name(constraint).empty()) {
            reject(OverlayContractStatus::UnknownValue,
                   "constraint kind is unknown");
        }
    }
    std::sort(selector.constraints.begin(), selector.constraints.end(),
              [](ConstraintKind left, ConstraintKind right) {
                  return wire_name(left) < wire_name(right);
              });
    if (std::adjacent_find(selector.constraints.begin(),
                           selector.constraints.end()) !=
        selector.constraints.end()) {
        reject(OverlayContractStatus::Duplicate,
               "catalog selector constraints contain duplicates");
    }
    if (selector.material_profiles.empty() ||
        selector.material_profiles.size() > 32) {
        reject(
            OverlayContractStatus::LimitExceeded,
            "catalog selector material profiles are empty or exceed the limit");
    }
    for (const auto &[key, value] : selector.material_profiles) {
        require_identifier(key, "material profile key");
        require_identifier(value, "material profile value");
    }
}

json catalog_selector_document(const RuntimeCatalogSelector &selector) {
    json constraints = json::array();
    for (const auto constraint : selector.constraints) {
        constraints.push_back(wire_name(constraint));
    }
    return json{
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

RuntimeCatalogSelector parse_catalog_selector(const json &value) {
    require_exact_keys(value,
                       {"backend_channel", "base_variant", "constraints",
                        "material_profiles", "model_type", "operation_kind",
                        "operation_template", "platform", "recovery",
                        "source_support_baseline"},
                       "catalog selector");
    const auto &constraint_values = required(value, "constraints");
    if (!constraint_values.is_array()) {
        reject(OverlayContractStatus::InvalidValue,
               "catalog selector constraints must be an array");
    }
    std::vector<ConstraintKind> constraints;
    constraints.reserve(constraint_values.size());
    for (const auto &constraint : constraint_values) {
        constraints.push_back(parse_constraint_kind(
            require_string(constraint, "catalog selector constraint")));
    }

    const auto &profile_values = required(value, "material_profiles");
    if (!profile_values.is_object()) {
        reject(OverlayContractStatus::InvalidValue,
               "catalog selector material profiles must be an object");
    }
    std::map<std::string, std::string> profiles;
    for (const auto &[key, profile] : profile_values.items()) {
        profiles.emplace(key,
                         require_string(profile, "material profile value"));
    }

    return RuntimeCatalogSelector{
        require_string(required(value, "source_support_baseline"),
                       "source support baseline"),
        require_string(required(value, "base_variant"), "base variant"),
        require_string(required(value, "platform"), "platform"),
        require_string(required(value, "backend_channel"), "backend channel"),
        require_string(required(value, "model_type"), "model type"),
        parse_operation_template_value(require_string(
            required(value, "operation_template"), "operation template")),
        parse_operation_kind_value(require_string(
            required(value, "operation_kind"), "operation kind")),
        std::move(constraints),
        require_string(required(value, "recovery"), "recovery contract"),
        std::move(profiles),
    };
}

void normalize_selector(LocalOverlaySelectorIdentity &selector) {
    require_digest(selector.catalog_sha256, "catalog digest");
    normalize_catalog_selector(selector.catalog_selector);
    require_identifier(selector.canonical_model_id, "canonical model ID");
    require_digest(selector.model_artifact_sha256, "model artifact digest");
    require_digest(selector.backend_build_sha256, "backend build digest");
    require_digest(selector.device_identity_sha256, "device identity digest");
    require_digest(selector.topology_sha256, "topology digest");
    require_digest(selector.dependency_set_sha256, "dependency set digest");
    require_digest(selector.driver_identity_sha256, "driver identity digest");
    require_digest(selector.configuration_sha256, "configuration digest");
    require_digest(selector.workload_sha256, "workload digest");
    require_digest(selector.operation_contract_sha256,
                   "operation contract digest");
}

json selector_document(const LocalOverlaySelectorIdentity &selector) {
    return json{
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

LocalOverlaySelectorIdentity parse_selector(const json &value) {
    require_exact_keys(value,
                       {"backend_build_sha256", "canonical_model_id", "catalog",
                        "catalog_sha256", "configuration_sha256",
                        "dependency_set_sha256", "device_identity_sha256",
                        "driver_identity_sha256", "model_artifact_sha256",
                        "operation_contract_sha256", "topology_sha256",
                        "workload_sha256"},
                       "local overlay selector");
    return LocalOverlaySelectorIdentity{
        require_string(required(value, "catalog_sha256"), "catalog digest"),
        parse_catalog_selector(required(value, "catalog")),
        require_string(required(value, "canonical_model_id"),
                       "canonical model ID"),
        require_string(required(value, "model_artifact_sha256"),
                       "model artifact digest"),
        require_string(required(value, "backend_build_sha256"),
                       "backend build digest"),
        require_string(required(value, "device_identity_sha256"),
                       "device identity digest"),
        require_string(required(value, "topology_sha256"), "topology digest"),
        require_string(required(value, "dependency_set_sha256"),
                       "dependency set digest"),
        require_string(required(value, "driver_identity_sha256"),
                       "driver identity digest"),
        require_string(required(value, "configuration_sha256"),
                       "configuration digest"),
        require_string(required(value, "workload_sha256"), "workload digest"),
        require_string(required(value, "operation_contract_sha256"),
                       "operation contract digest"),
    };
}

std::string selector_digest(const LocalOverlaySelectorIdentity &selector) {
    const auto bytes = selector_document(selector).dump();
    return sha256_hex(std::string_view(selector_identity_domain,
                                       sizeof(selector_identity_domain) - 1),
                      bytes);
}

std::string_view claim_family_wire(ClaimFamily family) noexcept {
    switch (family) {
    case ClaimFamily::ConsumableCapacity:
        return "consumable_capacity";
    case ClaimFamily::SafetyFloor:
        return "safety_floor";
    case ClaimFamily::CardinalityPool:
        return "cardinality_pool";
    case ClaimFamily::CompatibilityExclusivity:
        return "compatibility_exclusivity";
    }
    return {};
}

std::string_view
claim_completeness_wire(ClaimCompleteness completeness) noexcept {
    switch (completeness) {
    case ClaimCompleteness::NotApplicable:
        return "not_applicable";
    case ClaimCompleteness::KnownZero:
        return "known_zero";
    case ClaimCompleteness::Bounded:
        return "bounded";
    case ClaimCompleteness::Unknown:
        return "unknown";
    }
    return {};
}

std::string_view claim_unit_wire(ClaimUnit unit) noexcept {
    switch (unit) {
    case ClaimUnit::Bytes:
        return "bytes";
    case ClaimUnit::Count:
        return "count";
    }
    return {};
}

ClaimFamily family_at(std::size_t index) {
    switch (index) {
    case 0:
        return ClaimFamily::ConsumableCapacity;
    case 1:
        return ClaimFamily::SafetyFloor;
    case 2:
        return ClaimFamily::CardinalityPool;
    case 3:
        return ClaimFamily::CompatibilityExclusivity;
    default:
        reject(OverlayContractStatus::InvalidClaimClosure,
               "claim family is invalid");
    }
}

ClaimFamily parse_claim_family(std::string_view wire) {
    if (wire == "consumable_capacity") {
        return ClaimFamily::ConsumableCapacity;
    }
    if (wire == "safety_floor") {
        return ClaimFamily::SafetyFloor;
    }
    if (wire == "cardinality_pool") {
        return ClaimFamily::CardinalityPool;
    }
    if (wire == "compatibility_exclusivity") {
        return ClaimFamily::CompatibilityExclusivity;
    }
    reject(OverlayContractStatus::UnknownValue, "claim family is unknown");
}

ClaimCompleteness parse_claim_completeness(std::string_view wire) {
    if (wire == "not_applicable") {
        return ClaimCompleteness::NotApplicable;
    }
    if (wire == "known_zero") {
        return ClaimCompleteness::KnownZero;
    }
    if (wire == "bounded") {
        return ClaimCompleteness::Bounded;
    }
    if (wire == "unknown") {
        return ClaimCompleteness::Unknown;
    }
    reject(OverlayContractStatus::UnknownValue,
           "claim completeness is unknown");
}

ClaimUnit parse_claim_unit(std::string_view wire) {
    if (wire == "bytes") {
        return ClaimUnit::Bytes;
    }
    if (wire == "count") {
        return ClaimUnit::Count;
    }
    reject(OverlayContractStatus::UnknownValue, "claim unit is unknown");
}

[[noreturn]] void reject_claims(const CheckedClaimSetResult &result) {
    switch (result.status) {
    case ClaimStatus::InvalidIdentifier:
        reject(OverlayContractStatus::InvalidIdentifier, result.diagnostic);
    case ClaimStatus::LimitExceeded:
        reject(OverlayContractStatus::LimitExceeded, result.diagnostic);
    case ClaimStatus::IncompleteClaimClosure:
        reject(OverlayContractStatus::IncompleteClaimClosure,
               result.diagnostic);
    case ClaimStatus::Overflow:
        reject(OverlayContractStatus::ClaimArithmeticOverflow,
               result.diagnostic);
    default:
        reject(OverlayContractStatus::InvalidClaimClosure, result.diagnostic);
    }
}

CheckedClaimSet checked_claims(std::vector<ClaimFamilyClosure> closure) {
    auto result = check_claim_closure(std::move(closure));
    if (!result.accepted()) {
        reject_claims(result);
    }
    return *std::move(result.claims);
}

CheckedClaimSet added_claims(const CheckedClaimSet &left,
                             const CheckedClaimSet &right) {
    auto result = checked_add(left, right);
    if (!result.accepted()) {
        reject_claims(result);
    }
    return *std::move(result.claims);
}

std::vector<ClaimFamilyClosure> claim_closure(const CheckedClaimSet &claims) {
    std::vector<ClaimFamilyClosure> result;
    result.reserve(claim_family_count);
    for (std::size_t index = 0; index < claim_family_count; ++index) {
        const auto family = family_at(index);
        ClaimFamilyClosure family_closure;
        family_closure.family = family;
        family_closure.completeness = claims.completeness(family);
        for (const auto &entry : claims.entries(family)) {
            family_closure.entries.push_back(
                ClaimAmount{entry.constraint_id, entry.unit, entry.amount});
        }
        result.push_back(std::move(family_closure));
    }
    return result;
}

json claim_closure_document(const std::vector<ClaimFamilyClosure> &closure) {
    json result = json::array();
    for (const auto &family : closure) {
        json entries = json::array();
        for (const auto &entry : family.entries) {
            entries.push_back(json{
                {"amount", entry.amount},
                {"constraint_id", entry.constraint_id},
                {"unit", claim_unit_wire(entry.unit)},
            });
        }
        result.push_back(json{
            {"completeness", claim_completeness_wire(family.completeness)},
            {"entries", std::move(entries)},
            {"family", claim_family_wire(family.family)},
        });
    }
    return result;
}

std::vector<ClaimFamilyClosure> parse_claim_closure(const json &value,
                                                    std::string_view label) {
    if (!value.is_array()) {
        reject(OverlayContractStatus::InvalidClaimClosure,
               std::string(label) + " must be an array");
    }
    if (value.size() > max_journal_array_entries) {
        reject(OverlayContractStatus::LimitExceeded,
               std::string(label) + " exceeds the array limit");
    }
    std::vector<ClaimFamilyClosure> closure;
    closure.reserve(value.size());
    for (const auto &family_value : value) {
        require_exact_keys(family_value, {"completeness", "entries", "family"},
                           "claim family");
        const auto &entry_values = required(family_value, "entries");
        if (!entry_values.is_array()) {
            reject(OverlayContractStatus::InvalidClaimClosure,
                   "claim entries must be an array");
        }
        if (entry_values.size() > max_journal_array_entries) {
            reject(OverlayContractStatus::LimitExceeded,
                   "claim family exceeds the array limit");
        }
        ClaimFamilyClosure family;
        family.family = parse_claim_family(
            require_string(required(family_value, "family"), "claim family"));
        family.completeness = parse_claim_completeness(require_string(
            required(family_value, "completeness"), "claim completeness"));
        for (const auto &entry_value : entry_values) {
            require_exact_keys(entry_value, {"amount", "constraint_id", "unit"},
                               "claim amount");
            family.entries.push_back(ClaimAmount{
                require_string(required(entry_value, "constraint_id"),
                               "constraint ID"),
                parse_claim_unit(require_string(required(entry_value, "unit"),
                                                "claim unit")),
                require_u64(required(entry_value, "amount"), "claim amount"),
            });
        }
        closure.push_back(std::move(family));
    }
    return closure;
}

std::string_view method_scope_wire(LocalOverlayMethodScope scope) noexcept {
    switch (scope) {
    case LocalOverlayMethodScope::DeploymentExact:
        return "deployment_exact";
    case LocalOverlayMethodScope::ArchitecturePredicate:
        return "architecture_predicate";
    }
    return {};
}

LocalOverlayMethodScope parse_method_scope(std::string_view wire) {
    if (wire == "deployment_exact") {
        return LocalOverlayMethodScope::DeploymentExact;
    }
    if (wire == "architecture_predicate") {
        return LocalOverlayMethodScope::ArchitecturePredicate;
    }
    reject(OverlayContractStatus::UnknownValue,
           "local overlay method scope is unknown");
}

void normalize_method(LocalOverlayMethodIdentity &method) {
    require_identifier(method.method_id, "method ID");
    require_digest(method.method_revision_sha256, "method revision digest");
    if (method_scope_wire(method.scope).empty() ||
        wire_name(method.operation_kind).empty()) {
        reject(OverlayContractStatus::UnknownValue,
               "local overlay method contains an unknown closed value");
    }
    if (method.scope == LocalOverlayMethodScope::DeploymentExact) {
        if (method.architecture_predicate_sha256.has_value()) {
            reject(OverlayContractStatus::InvalidValue,
                   "deployment-exact method cannot carry an architecture "
                   "predicate");
        }
    } else {
        if (!method.architecture_predicate_sha256.has_value()) {
            reject(OverlayContractStatus::IncompleteIdentity,
                   "architecture-scoped method is missing its predicate");
        }
        require_digest(*method.architecture_predicate_sha256,
                       "architecture predicate digest");
    }
    require_digest(method.calibration_revision_sha256,
                   "calibration revision digest");
}

json method_document(const LocalOverlayMethodIdentity &method) {
    return json{
        {"architecture_predicate_sha256",
         method.architecture_predicate_sha256.has_value()
             ? json(*method.architecture_predicate_sha256)
             : json(nullptr)},
        {"calibration_revision_sha256", method.calibration_revision_sha256},
        {"method_id", method.method_id},
        {"method_revision_sha256", method.method_revision_sha256},
        {"operation_kind", wire_name(method.operation_kind)},
        {"scope", method_scope_wire(method.scope)},
    };
}

LocalOverlayMethodIdentity parse_method(const json &value) {
    require_exact_keys(value,
                       {"architecture_predicate_sha256",
                        "calibration_revision_sha256", "method_id",
                        "method_revision_sha256", "operation_kind", "scope"},
                       "local overlay method");
    std::optional<std::string> architecture_predicate;
    const auto &predicate_value =
        required(value, "architecture_predicate_sha256");
    if (!predicate_value.is_null()) {
        architecture_predicate =
            require_string(predicate_value, "architecture predicate digest");
    }
    return LocalOverlayMethodIdentity{
        require_string(required(value, "method_id"), "method ID"),
        require_string(required(value, "method_revision_sha256"),
                       "method revision digest"),
        parse_method_scope(
            require_string(required(value, "scope"), "method scope")),
        parse_operation_kind_value(require_string(
            required(value, "operation_kind"), "operation kind")),
        std::move(architecture_predicate),
        require_string(required(value, "calibration_revision_sha256"),
                       "calibration revision digest"),
    };
}

std::string method_digest(const LocalOverlayMethodIdentity &method) {
    const auto bytes = method_document(method).dump();
    return sha256_hex(std::string_view(method_identity_domain,
                                       sizeof(method_identity_domain) - 1),
                      bytes);
}

void normalize_generations(const OverlaySourceGenerations &generations) {
    if (generations.model == 0 || generations.backend == 0 ||
        generations.device == 0 || generations.topology == 0 ||
        generations.driver == 0 || generations.configuration == 0 ||
        generations.workload == 0) {
        reject(OverlayContractStatus::IncompleteIdentity,
               "profiling input source generations must all be nonzero");
    }
}

json generations_document(const OverlaySourceGenerations &generations) {
    return json{
        {"backend", generations.backend},
        {"configuration", generations.configuration},
        {"device", generations.device},
        {"driver", generations.driver},
        {"model", generations.model},
        {"topology", generations.topology},
        {"workload", generations.workload},
    };
}

OverlaySourceGenerations parse_generations(const json &value) {
    require_exact_keys(value,
                       {"backend", "configuration", "device", "driver", "model",
                        "topology", "workload"},
                       "source generations");
    return OverlaySourceGenerations{
        require_u64(required(value, "model"), "model generation"),
        require_u64(required(value, "backend"), "backend generation"),
        require_u64(required(value, "device"), "device generation"),
        require_u64(required(value, "topology"), "topology generation"),
        require_u64(required(value, "driver"), "driver generation"),
        require_u64(required(value, "configuration"),
                    "configuration generation"),
        require_u64(required(value, "workload"), "workload generation"),
    };
}

std::string canonical_document(json payload, std::string_view checksum);

std::string_view profiling_phase_wire(ProfilingPhase phase) noexcept {
    switch (phase) {
    case ProfilingPhase::Baseline:
        return "baseline";
    case ProfilingPhase::Workload:
        return "workload";
    case ProfilingPhase::Release:
        return "release";
    }
    return {};
}

ProfilingPhase parse_profiling_phase(std::string_view wire) {
    if (wire == "baseline") {
        return ProfilingPhase::Baseline;
    }
    if (wire == "workload") {
        return ProfilingPhase::Workload;
    }
    if (wire == "release") {
        return ProfilingPhase::Release;
    }
    reject(OverlayContractStatus::UnknownValue, "profiling phase is unknown");
}

std::string_view
profiling_health_wire(ProfilingObservationHealth health) noexcept {
    switch (health) {
    case ProfilingObservationHealth::Valid:
        return "valid";
    case ProfilingObservationHealth::Missing:
        return "missing";
    case ProfilingObservationHealth::Stale:
        return "stale";
    case ProfilingObservationHealth::Unhealthy:
        return "unhealthy";
    case ProfilingObservationHealth::Incoherent:
        return "incoherent";
    case ProfilingObservationHealth::Superseded:
        return "superseded";
    }
    return {};
}

ProfilingObservationHealth parse_profiling_health(std::string_view wire) {
    if (wire == "valid") {
        return ProfilingObservationHealth::Valid;
    }
    if (wire == "missing") {
        return ProfilingObservationHealth::Missing;
    }
    if (wire == "stale") {
        return ProfilingObservationHealth::Stale;
    }
    if (wire == "unhealthy") {
        return ProfilingObservationHealth::Unhealthy;
    }
    if (wire == "incoherent") {
        return ProfilingObservationHealth::Incoherent;
    }
    if (wire == "superseded") {
        return ProfilingObservationHealth::Superseded;
    }
    reject(OverlayContractStatus::UnknownValue,
           "profiling observation health is unknown");
}

std::string_view
profiling_owner_coverage_wire(ProfilingOwnerCoverage coverage) noexcept {
    switch (coverage) {
    case ProfilingOwnerCoverage::Complete:
        return "complete";
    case ProfilingOwnerCoverage::Incomplete:
        return "incomplete";
    case ProfilingOwnerCoverage::Unknown:
        return "unknown";
    }
    return {};
}

ProfilingOwnerCoverage parse_profiling_owner_coverage(std::string_view wire) {
    if (wire == "complete") {
        return ProfilingOwnerCoverage::Complete;
    }
    if (wire == "incomplete") {
        return ProfilingOwnerCoverage::Incomplete;
    }
    if (wire == "unknown") {
        return ProfilingOwnerCoverage::Unknown;
    }
    reject(OverlayContractStatus::UnknownValue,
           "profiling owner coverage is unknown");
}

std::string_view
profiling_lifecycle_state_wire(ProfilingLifecycleState state) noexcept {
    switch (state) {
    case ProfilingLifecycleState::BaselineQuiescent:
        return "baseline_quiescent";
    case ProfilingLifecycleState::WorkloadComplete:
        return "workload_complete";
    case ProfilingLifecycleState::ReleaseVerified:
        return "release_verified";
    }
    return {};
}

ProfilingLifecycleState
parse_profiling_lifecycle_state(std::string_view wire) {
    if (wire == "baseline_quiescent") {
        return ProfilingLifecycleState::BaselineQuiescent;
    }
    if (wire == "workload_complete") {
        return ProfilingLifecycleState::WorkloadComplete;
    }
    if (wire == "release_verified") {
        return ProfilingLifecycleState::ReleaseVerified;
    }
    reject(OverlayContractStatus::UnknownValue,
           "profiling lifecycle state is unknown");
}

ProfilingLifecycleState expected_lifecycle_state(ProfilingPhase phase) {
    switch (phase) {
    case ProfilingPhase::Baseline:
        return ProfilingLifecycleState::BaselineQuiescent;
    case ProfilingPhase::Workload:
        return ProfilingLifecycleState::WorkloadComplete;
    case ProfilingPhase::Release:
        return ProfilingLifecycleState::ReleaseVerified;
    }
    reject(OverlayContractStatus::UnknownValue, "profiling phase is unknown");
}

std::vector<ClaimFamilyClosure>
normalize_phase_claims(std::vector<ClaimFamilyClosure> closure) {
    return claim_closure(checked_claims(std::move(closure)));
}

bool has_positive_claim(
    const std::vector<ClaimFamilyClosure> &closure) noexcept {
    for (const auto &family : closure) {
        for (const auto &entry : family.entries) {
            if (entry.amount > 0) {
                return true;
            }
        }
    }
    return false;
}

void normalize_profiling_phase_draft(ProfilingPhaseAttestationDraft &draft) {
    if (!schema_is_supported(draft.schema)) {
        reject(OverlayContractStatus::UnsupportedSchema,
               "profiling phase attestation schema is unsupported");
    }
    if (profiling_phase_wire(draft.phase).empty()) {
        reject(OverlayContractStatus::UnknownValue,
               "profiling phase is unknown");
    }
    require_digest(draft.deployment_id, "deployment ID");
    require_identifier(draft.profiling_transaction_id,
                       "profiling transaction ID");
    require_digest(draft.selector_sha256, "selector digest");
    require_identifier(draft.provider_id, "provider ID");
    require_digest(draft.provider_revision_sha256,
                   "provider revision digest");
    require_digest(draft.provenance_sha256, "provenance digest");
    require_digest(draft.observation_contract_sha256,
                   "observation contract digest");
    require_digest(draft.predictor_contract_sha256,
                   "predictor contract digest");
    normalize_generations(draft.generations);
    if (draft.observation_generation == 0) {
        reject(OverlayContractStatus::IncompleteIdentity,
               "observation generation must be nonzero");
    }
    require_advancing_time(draft.observed_at, draft.fresh_until,
                           "fresh-until timestamp");
    if (draft.max_source_skew_milliseconds == 0 ||
        draft.source_skew_milliseconds >
            draft.max_source_skew_milliseconds) {
        reject(OverlayContractStatus::InvalidValue,
               "source skew exceeds the accepted bound");
    }
    if (draft.health != ProfilingObservationHealth::Valid) {
        reject(OverlayContractStatus::InvalidValue,
               "profiling observation health is not valid");
    }
    if (draft.owner_coverage != ProfilingOwnerCoverage::Complete) {
        reject(OverlayContractStatus::IncompleteIdentity,
               "profiling owner coverage is incomplete");
    }
    if (draft.lifecycle_state != expected_lifecycle_state(draft.phase)) {
        reject(OverlayContractStatus::InvalidValue,
               "profiling phase and lifecycle state do not match");
    }

    draft.observed_claims =
        normalize_phase_claims(std::move(draft.observed_claims));
    draft.attributed_claims =
        normalize_phase_claims(std::move(draft.attributed_claims));
    draft.external_change_claims =
        normalize_phase_claims(std::move(draft.external_change_claims));
    draft.unattributed_claims =
        normalize_phase_claims(std::move(draft.unattributed_claims));
    draft.uncertainty_claims =
        normalize_phase_claims(std::move(draft.uncertainty_claims));
    draft.safety_margin_claims =
        normalize_phase_claims(std::move(draft.safety_margin_claims));

    if (has_positive_claim(draft.external_change_claims) ||
        has_positive_claim(draft.unattributed_claims)) {
        reject(OverlayContractStatus::InvalidClaimClosure,
               "profiling evidence contains external or unattributed change");
    }
    if (!has_positive_claim(draft.safety_margin_claims)) {
        reject(OverlayContractStatus::InvalidClaimClosure,
               "safety-margin claims must contain positive bounded slack");
    }
}

json profiling_phase_payload(const ProfilingPhaseAttestationDraft &draft) {
    return json{
        {"attributed_claims", claim_closure_document(draft.attributed_claims)},
        {"deployment_id", draft.deployment_id},
        {"external_change_claims",
         claim_closure_document(draft.external_change_claims)},
        {"fresh_until", draft.fresh_until},
        {"generations", generations_document(draft.generations)},
        {"health", profiling_health_wire(draft.health)},
        {"lifecycle_state",
         profiling_lifecycle_state_wire(draft.lifecycle_state)},
        {"max_source_skew_milliseconds",
         draft.max_source_skew_milliseconds},
        {"observation_contract_sha256",
         draft.observation_contract_sha256},
        {"observation_generation", draft.observation_generation},
        {"observed_at", draft.observed_at},
        {"observed_claims", claim_closure_document(draft.observed_claims)},
        {"owner_coverage",
         profiling_owner_coverage_wire(draft.owner_coverage)},
        {"phase", profiling_phase_wire(draft.phase)},
        {"predictor_contract_sha256", draft.predictor_contract_sha256},
        {"profiling_transaction_id", draft.profiling_transaction_id},
        {"provenance_sha256", draft.provenance_sha256},
        {"provider_id", draft.provider_id},
        {"provider_revision_sha256", draft.provider_revision_sha256},
        {"safety_margin_claims",
         claim_closure_document(draft.safety_margin_claims)},
        {"schema", schema_document(draft.schema)},
        {"selector_sha256", draft.selector_sha256},
        {"source_skew_milliseconds", draft.source_skew_milliseconds},
        {"unattributed_claims",
         claim_closure_document(draft.unattributed_claims)},
        {"uncertainty_claims",
         claim_closure_document(draft.uncertainty_claims)},
    };
}

struct SealedProfilingPhaseData {
    ProfilingPhaseAttestationDraft draft;
    std::string checksum;
    std::string canonical;
};

SealedProfilingPhaseData
seal_profiling_phase_data(ProfilingPhaseAttestationDraft draft) {
    normalize_profiling_phase_draft(draft);
    const auto payload = profiling_phase_payload(draft);
    const auto payload_bytes = payload.dump();
    const auto checksum = sha256_hex(
        std::string_view(profiling_phase_attestation_domain,
                         sizeof(profiling_phase_attestation_domain) - 1),
        payload_bytes);
    auto canonical = canonical_document(payload, checksum);
    if (canonical.size() > max_local_overlay_input_bytes) {
        reject(OverlayContractStatus::LimitExceeded,
               "sealed profiling phase attestation exceeds the input limit");
    }
    return SealedProfilingPhaseData{std::move(draft), checksum,
                                    std::move(canonical)};
}

struct ParsedProfilingPhaseDocument {
    ProfilingPhaseAttestationDraft draft;
    std::string checksum;
};

ParsedProfilingPhaseDocument
parse_profiling_phase_document(const json &document) {
    require_exact_keys(
        document,
        {"attributed_claims", "checksum_sha256", "deployment_id",
         "external_change_claims", "fresh_until", "generations", "health",
         "lifecycle_state", "max_source_skew_milliseconds",
         "observation_contract_sha256", "observation_generation",
         "observed_at", "observed_claims", "owner_coverage", "phase",
         "predictor_contract_sha256", "profiling_transaction_id",
         "provenance_sha256", "provider_id", "provider_revision_sha256",
         "safety_margin_claims", "schema", "selector_sha256",
         "source_skew_milliseconds", "unattributed_claims",
         "uncertainty_claims"},
        "profiling phase attestation");
    ProfilingPhaseAttestationDraft draft;
    draft.schema = parse_schema(required(document, "schema"));
    draft.phase = parse_profiling_phase(
        require_string(required(document, "phase"), "profiling phase"));
    draft.deployment_id =
        require_string(required(document, "deployment_id"), "deployment ID");
    draft.profiling_transaction_id =
        require_string(required(document, "profiling_transaction_id"),
                       "profiling transaction ID");
    draft.selector_sha256 = require_string(
        required(document, "selector_sha256"), "selector digest");
    draft.provider_id =
        require_string(required(document, "provider_id"), "provider ID");
    draft.provider_revision_sha256 = require_string(
        required(document, "provider_revision_sha256"),
        "provider revision digest");
    draft.provenance_sha256 = require_string(
        required(document, "provenance_sha256"), "provenance digest");
    draft.observation_contract_sha256 = require_string(
        required(document, "observation_contract_sha256"),
        "observation contract digest");
    draft.predictor_contract_sha256 = require_string(
        required(document, "predictor_contract_sha256"),
        "predictor contract digest");
    draft.generations = parse_generations(required(document, "generations"));
    draft.observation_generation =
        require_u64(required(document, "observation_generation"),
                    "observation generation");
    draft.observed_at =
        require_string(required(document, "observed_at"), "observed timestamp");
    draft.fresh_until = require_string(required(document, "fresh_until"),
                                       "fresh-until timestamp");
    draft.source_skew_milliseconds =
        require_u64(required(document, "source_skew_milliseconds"),
                    "source skew");
    draft.max_source_skew_milliseconds =
        require_u64(required(document, "max_source_skew_milliseconds"),
                    "maximum source skew");
    draft.health = parse_profiling_health(require_string(
        required(document, "health"), "profiling observation health"));
    draft.owner_coverage = parse_profiling_owner_coverage(require_string(
        required(document, "owner_coverage"), "profiling owner coverage"));
    draft.observed_claims = parse_claim_closure(
        required(document, "observed_claims"), "observed claims");
    draft.attributed_claims = parse_claim_closure(
        required(document, "attributed_claims"), "attributed claims");
    draft.external_change_claims = parse_claim_closure(
        required(document, "external_change_claims"),
        "external-change claims");
    draft.unattributed_claims = parse_claim_closure(
        required(document, "unattributed_claims"), "unattributed claims");
    draft.uncertainty_claims = parse_claim_closure(
        required(document, "uncertainty_claims"), "uncertainty claims");
    draft.safety_margin_claims = parse_claim_closure(
        required(document, "safety_margin_claims"), "safety-margin claims");
    draft.lifecycle_state = parse_profiling_lifecycle_state(require_string(
        required(document, "lifecycle_state"), "profiling lifecycle state"));
    return ParsedProfilingPhaseDocument{
        std::move(draft),
        require_string(required(document, "checksum_sha256"),
                       "profiling phase attestation checksum"),
    };
}

void normalize_profiling_draft(ProfilingInputEnvelopeDraft &draft) {
    if (!schema_is_supported(draft.schema)) {
        reject(OverlayContractStatus::UnsupportedSchema,
               "overlay schema is unsupported");
    }
    require_digest(draft.deployment_id, "deployment ID");
    if (draft.sequence == 0) {
        reject(OverlayContractStatus::InvalidSequence,
               "profiling input sequence must be nonzero");
    }
    require_identifier(draft.profiling_transaction_id,
                       "profiling transaction ID");
    normalize_selector(draft.selector);
    normalize_generations(draft.generations);
    const auto checked = checked_claims(std::move(draft.attributed_claims));
    if (!claim_families_cover_ordinary_constraints(
            checked, draft.selector.catalog_selector.constraints)) {
        reject(OverlayContractStatus::IncompleteClaimClosure,
               "attributed claims omit a required selector family");
    }
    draft.attributed_claims = claim_closure(checked);
    require_digest(draft.baseline_observation_sha256,
                   "baseline observation digest");
    require_digest(draft.workload_observation_sha256,
                   "workload observation digest");
    require_digest(draft.release_observation_sha256,
                   "release observation digest");
    require_digest(draft.observation_contract_sha256,
                   "observation contract digest");
    require_digest(draft.predictor_contract_sha256,
                   "predictor contract digest");
    require_advancing_time(draft.observed_at, draft.fresh_until,
                           "fresh-until timestamp");
    if (draft.max_clock_skew_milliseconds == 0) {
        reject(OverlayContractStatus::InvalidValue,
               "maximum clock skew must be nonzero");
    }
    if (!draft.attribution_complete || !draft.external_demand_absent ||
        !draft.lifecycle_release_verified) {
        reject(OverlayContractStatus::IncompleteIdentity,
               "profiling input does not close attribution and lifecycle "
               "evidence");
    }
}

json profiling_payload(const ProfilingInputEnvelopeDraft &draft) {
    return json{
        {"attributed_claims", claim_closure_document(draft.attributed_claims)},
        {"attribution_complete", draft.attribution_complete},
        {"baseline_observation_sha256", draft.baseline_observation_sha256},
        {"deployment_id", draft.deployment_id},
        {"external_demand_absent", draft.external_demand_absent},
        {"fresh_until", draft.fresh_until},
        {"generations", generations_document(draft.generations)},
        {"lifecycle_release_verified", draft.lifecycle_release_verified},
        {"max_clock_skew_milliseconds", draft.max_clock_skew_milliseconds},
        {"observation_contract_sha256", draft.observation_contract_sha256},
        {"observed_at", draft.observed_at},
        {"predictor_contract_sha256", draft.predictor_contract_sha256},
        {"profiling_transaction_id", draft.profiling_transaction_id},
        {"release_observation_sha256", draft.release_observation_sha256},
        {"schema", schema_document(draft.schema)},
        {"selector", selector_document(draft.selector)},
        {"sequence", draft.sequence},
        {"workload_observation_sha256", draft.workload_observation_sha256},
    };
}

std::string canonical_document(json payload, std::string_view checksum) {
    payload["checksum_sha256"] = checksum;
    return payload.dump();
}

struct SealedProfilingData {
    ProfilingInputEnvelopeDraft draft;
    std::string selector_sha256;
    std::string checksum;
    std::string canonical;
};

SealedProfilingData seal_profiling_data(ProfilingInputEnvelopeDraft draft) {
    normalize_profiling_draft(draft);
    const auto selector_sha = selector_digest(draft.selector);
    const auto payload = profiling_payload(draft);
    const auto payload_bytes = payload.dump();
    const auto checksum =
        sha256_hex(std::string_view(profiling_input_domain,
                                    sizeof(profiling_input_domain) - 1),
                   payload_bytes);
    auto canonical = canonical_document(payload, checksum);
    if (canonical.size() > max_local_overlay_input_bytes) {
        reject(OverlayContractStatus::LimitExceeded,
               "sealed profiling input exceeds the input limit");
    }
    return SealedProfilingData{std::move(draft), selector_sha, checksum,
                               std::move(canonical)};
}

struct ParsedProfilingDocument {
    ProfilingInputEnvelopeDraft draft;
    std::string checksum;
};

ParsedProfilingDocument parse_profiling_document(const json &document) {
    require_exact_keys(
        document,
        {"attributed_claims", "attribution_complete",
         "baseline_observation_sha256", "checksum_sha256", "deployment_id",
         "external_demand_absent", "fresh_until", "generations",
         "lifecycle_release_verified", "max_clock_skew_milliseconds",
         "observation_contract_sha256", "observed_at",
         "predictor_contract_sha256", "profiling_transaction_id",
         "release_observation_sha256", "schema", "selector", "sequence",
         "workload_observation_sha256"},
        "profiling input");
    ProfilingInputEnvelopeDraft draft;
    draft.schema = parse_schema(required(document, "schema"));
    draft.deployment_id =
        require_string(required(document, "deployment_id"), "deployment ID");
    draft.sequence =
        require_u64(required(document, "sequence"), "profiling input sequence");
    draft.profiling_transaction_id =
        require_string(required(document, "profiling_transaction_id"),
                       "profiling transaction ID");
    draft.selector = parse_selector(required(document, "selector"));
    draft.generations = parse_generations(required(document, "generations"));
    draft.attributed_claims = parse_claim_closure(
        required(document, "attributed_claims"), "attributed claims");
    draft.baseline_observation_sha256 =
        require_string(required(document, "baseline_observation_sha256"),
                       "baseline observation digest");
    draft.workload_observation_sha256 =
        require_string(required(document, "workload_observation_sha256"),
                       "workload observation digest");
    draft.release_observation_sha256 =
        require_string(required(document, "release_observation_sha256"),
                       "release observation digest");
    draft.observation_contract_sha256 =
        require_string(required(document, "observation_contract_sha256"),
                       "observation contract digest");
    draft.predictor_contract_sha256 =
        require_string(required(document, "predictor_contract_sha256"),
                       "predictor contract digest");
    draft.observed_at =
        require_string(required(document, "observed_at"), "observed timestamp");
    draft.fresh_until = require_string(required(document, "fresh_until"),
                                       "fresh-until timestamp");
    draft.max_clock_skew_milliseconds =
        require_u64(required(document, "max_clock_skew_milliseconds"),
                    "maximum clock skew");
    draft.attribution_complete = require_bool(
        required(document, "attribution_complete"), "attribution completeness");
    draft.external_demand_absent =
        require_bool(required(document, "external_demand_absent"),
                     "external-demand absence");
    draft.lifecycle_release_verified =
        require_bool(required(document, "lifecycle_release_verified"),
                     "lifecycle release verification");
    return ParsedProfilingDocument{
        std::move(draft),
        require_string(required(document, "checksum_sha256"),
                       "profiling input checksum"),
    };
}

std::string_view overlay_status_wire(LocalOverlayObjectStatus status) noexcept {
    switch (status) {
    case LocalOverlayObjectStatus::Qualified:
        return "qualified";
    }
    return {};
}

LocalOverlayObjectStatus parse_overlay_status(std::string_view wire) {
    if (wire == "qualified") {
        return LocalOverlayObjectStatus::Qualified;
    }
    reject(OverlayContractStatus::UnknownValue,
           "local overlay object status is unknown");
}

struct NormalizedOverlayClaims {
    std::vector<ClaimFamilyClosure> bound;
    std::vector<ClaimFamilyClosure> uncertainty;
    std::vector<ClaimFamilyClosure> safety_margin;
    std::vector<ClaimFamilyClosure> conservative;
};

bool has_positive_bounded_entry(const CheckedClaimSet &claims) {
    for (std::size_t index = 0; index < claim_family_count; ++index) {
        const auto family = family_at(index);
        if (claims.completeness(family) != ClaimCompleteness::Bounded) {
            continue;
        }
        const auto &entries = claims.entries(family);
        if (std::any_of(
                entries.begin(), entries.end(),
                [](const ClaimTotal &entry) { return entry.amount > 0; })) {
            return true;
        }
    }
    return false;
}

NormalizedOverlayClaims
normalize_overlay_claims(LocalOverlayObjectDraft &draft) {
    const auto bound = checked_claims(std::move(draft.bound_claims));
    const auto uncertainty =
        checked_claims(std::move(draft.uncertainty_claims));
    const auto safety_margin =
        checked_claims(std::move(draft.safety_margin_claims));
    if (!has_positive_bounded_entry(safety_margin)) {
        reject(
            OverlayContractStatus::InvalidClaimClosure,
            "safety-margin claim closure must contain positive bounded slack");
    }
    const auto bound_and_uncertainty = added_claims(bound, uncertainty);
    const auto conservative =
        added_claims(bound_and_uncertainty, safety_margin);
    auto conservative_closure = claim_closure(conservative);
    const auto rechecked_conservative =
        check_claim_closure(conservative_closure);
    if (!rechecked_conservative.accepted()) {
        reject_claims(rechecked_conservative);
    }
    NormalizedOverlayClaims result{
        claim_closure(bound),
        claim_closure(uncertainty),
        claim_closure(safety_margin),
        std::move(conservative_closure),
    };
    draft.bound_claims = result.bound;
    draft.uncertainty_claims = result.uncertainty;
    draft.safety_margin_claims = result.safety_margin;
    return result;
}

std::vector<ClaimFamilyClosure>
normalize_overlay_draft(LocalOverlayObjectDraft &draft) {
    if (!schema_is_supported(draft.schema)) {
        reject(OverlayContractStatus::UnsupportedSchema,
               "overlay schema is unsupported");
    }
    require_digest(draft.deployment_id, "deployment ID");
    if (draft.sequence == 0) {
        reject(OverlayContractStatus::InvalidSequence,
               "local overlay sequence must be nonzero");
    }
    require_digest(draft.profiling_input_sha256, "profiling input digest");
    normalize_selector(draft.selector);
    normalize_method(draft.method);
    if (draft.method.operation_kind !=
        draft.selector.catalog_selector.operation_kind) {
        reject(OverlayContractStatus::ReferenceMismatch,
               "local overlay method does not match the selector operation");
    }
    auto claims = normalize_overlay_claims(draft);
    if (draft.confidence_basis_points == 0 ||
        draft.confidence_basis_points > 10000) {
        reject(OverlayContractStatus::InvalidValue,
               "local overlay confidence must be between 1 and 10000 basis "
               "points");
    }
    require_advancing_time(draft.qualified_at, draft.expires_at,
                           "overlay expiry timestamp");
    if (overlay_status_wire(draft.status).empty()) {
        reject(OverlayContractStatus::UnknownValue,
               "local overlay object status is unknown");
    }
    require_digest(draft.decision_trace_sha256, "decision trace digest");
    return std::move(claims.conservative);
}

json overlay_payload(const LocalOverlayObjectDraft &draft) {
    return json{
        {"bound_claims", claim_closure_document(draft.bound_claims)},
        {"confidence_basis_points", draft.confidence_basis_points},
        {"decision_trace_sha256", draft.decision_trace_sha256},
        {"deployment_id", draft.deployment_id},
        {"expires_at", draft.expires_at},
        {"method", method_document(draft.method)},
        {"profiling_input_sha256", draft.profiling_input_sha256},
        {"qualified_at", draft.qualified_at},
        {"safety_margin_claims",
         claim_closure_document(draft.safety_margin_claims)},
        {"schema", schema_document(draft.schema)},
        {"selector", selector_document(draft.selector)},
        {"sequence", draft.sequence},
        {"status", overlay_status_wire(draft.status)},
        {"uncertainty_claims",
         claim_closure_document(draft.uncertainty_claims)},
    };
}

struct SealedOverlayData {
    LocalOverlayObjectDraft draft;
    std::vector<ClaimFamilyClosure> conservative_claims;
    std::string selector_sha256;
    std::string method_sha256;
    std::string checksum;
    std::string canonical;
};

SealedOverlayData seal_overlay_data(LocalOverlayObjectDraft draft) {
    auto conservative = normalize_overlay_draft(draft);
    const auto selector_sha = selector_digest(draft.selector);
    const auto method_sha = method_digest(draft.method);
    const auto payload = overlay_payload(draft);
    const auto payload_bytes = payload.dump();
    const auto checksum =
        sha256_hex(std::string_view(local_overlay_domain,
                                    sizeof(local_overlay_domain) - 1),
                   payload_bytes);
    auto canonical = canonical_document(payload, checksum);
    if (canonical.size() > max_local_overlay_input_bytes) {
        reject(OverlayContractStatus::LimitExceeded,
               "sealed local overlay exceeds the input limit");
    }
    return SealedOverlayData{
        std::move(draft), std::move(conservative), selector_sha, method_sha,
        checksum,         std::move(canonical),
    };
}

struct ParsedOverlayDocument {
    LocalOverlayObjectDraft draft;
    std::string checksum;
};

ParsedOverlayDocument parse_overlay_document(const json &document) {
    require_exact_keys(
        document,
        {"bound_claims", "checksum_sha256", "confidence_basis_points",
         "decision_trace_sha256", "deployment_id", "expires_at", "method",
         "profiling_input_sha256", "qualified_at", "safety_margin_claims",
         "schema", "selector", "sequence", "status", "uncertainty_claims"},
        "local overlay object");
    LocalOverlayObjectDraft draft;
    draft.schema = parse_schema(required(document, "schema"));
    draft.deployment_id =
        require_string(required(document, "deployment_id"), "deployment ID");
    draft.sequence =
        require_u64(required(document, "sequence"), "local overlay sequence");
    draft.profiling_input_sha256 = require_string(
        required(document, "profiling_input_sha256"), "profiling input digest");
    draft.selector = parse_selector(required(document, "selector"));
    draft.method = parse_method(required(document, "method"));
    draft.bound_claims =
        parse_claim_closure(required(document, "bound_claims"), "bound claims");
    draft.uncertainty_claims = parse_claim_closure(
        required(document, "uncertainty_claims"), "uncertainty claims");
    draft.safety_margin_claims = parse_claim_closure(
        required(document, "safety_margin_claims"), "safety margin claims");
    const auto confidence = require_u64(
        required(document, "confidence_basis_points"), "confidence");
    if (confidence > std::numeric_limits<std::uint32_t>::max()) {
        reject(OverlayContractStatus::InvalidValue,
               "local overlay confidence is invalid");
    }
    draft.confidence_basis_points = static_cast<std::uint32_t>(confidence);
    draft.qualified_at = require_string(required(document, "qualified_at"),
                                        "qualification timestamp");
    draft.expires_at =
        require_string(required(document, "expires_at"), "expiry timestamp");
    draft.status = parse_overlay_status(require_string(
        required(document, "status"), "local overlay object status"));
    draft.decision_trace_sha256 = require_string(
        required(document, "decision_trace_sha256"), "decision trace digest");
    return ParsedOverlayDocument{
        std::move(draft),
        require_string(required(document, "checksum_sha256"),
                       "local overlay checksum"),
    };
}

std::string_view
root_transition_wire(OverlayRootTransition transition) noexcept {
    switch (transition) {
    case OverlayRootTransition::Qualification:
        return "qualification";
    case OverlayRootTransition::Rollback:
        return "rollback";
    }
    return {};
}

OverlayRootTransition parse_root_transition(std::string_view wire) {
    if (wire == "qualification") {
        return OverlayRootTransition::Qualification;
    }
    if (wire == "rollback") {
        return OverlayRootTransition::Rollback;
    }
    reject(OverlayContractStatus::UnknownValue,
           "activation-root transition is unknown");
}

std::string_view authority_status_wire(OverlayAuthorityStatus status) noexcept {
    switch (status) {
    case OverlayAuthorityStatus::Active:
        return "active";
    }
    return {};
}

OverlayAuthorityStatus parse_authority_status(std::string_view wire) {
    if (wire == "active") {
        return OverlayAuthorityStatus::Active;
    }
    reject(OverlayContractStatus::UnknownValue,
           "activation-root authority status is unknown");
}

void normalize_root_draft(OverlayActivationRootDraft &draft) {
    if (!schema_is_supported(draft.schema)) {
        reject(OverlayContractStatus::UnsupportedSchema,
               "overlay schema is unsupported");
    }
    require_digest(draft.deployment_id, "deployment ID");
    if (draft.generation == 0) {
        reject(OverlayContractStatus::InvalidSequence,
               "activation-root generation must be nonzero");
    }
    if (draft.generation == 1) {
        if (draft.previous_root_sha256.has_value() ||
            draft.transition != OverlayRootTransition::Qualification) {
            reject(OverlayContractStatus::InvalidSequence,
                   "activation-root genesis must be a qualification without a "
                   "predecessor");
        }
    } else {
        if (!draft.previous_root_sha256.has_value()) {
            reject(OverlayContractStatus::InvalidSequence,
                   "activation-root successor is missing its predecessor");
        }
        require_digest(*draft.previous_root_sha256,
                       "previous activation-root digest");
    }
    if (root_transition_wire(draft.transition).empty() ||
        authority_status_wire(draft.authority_status).empty()) {
        reject(OverlayContractStatus::UnknownValue,
               "activation root contains an unknown closed value");
    }
    require_digest(draft.selected_overlay_sha256, "selected overlay digest");
    require_digest(draft.selected_selector_sha256, "selected selector digest");
    require_digest(draft.selected_method_sha256, "selected method digest");
    require_digest(draft.decision_trace_sha256, "decision trace digest");
    if (draft.selected_overlay_sequence == 0 ||
        draft.sequence_high_water == 0 ||
        draft.selected_overlay_sequence > draft.sequence_high_water) {
        reject(OverlayContractStatus::InvalidSequence,
               "activation root has an invalid overlay sequence");
    }
    if (draft.generation == 1 &&
        (draft.selected_overlay_sequence != 1 ||
         draft.sequence_high_water != 1)) {
        reject(OverlayContractStatus::InvalidSequence,
               "activation-root genesis must select overlay sequence one");
    }
    if (draft.transition == OverlayRootTransition::Qualification &&
        draft.selected_overlay_sequence != draft.sequence_high_water) {
        reject(OverlayContractStatus::InvalidSequence,
               "qualification must select the overlay sequence high-water");
    }
    if (draft.transition == OverlayRootTransition::Rollback &&
        (draft.generation == 1 ||
         draft.selected_overlay_sequence >= draft.sequence_high_water)) {
        reject(OverlayContractStatus::InvalidSequence,
               "rollback must select an earlier immutable overlay sequence");
    }
    require_advancing_time(draft.activated_at, draft.expires_at,
                           "activation-root expiry timestamp");
}

json root_payload(const OverlayActivationRootDraft &draft) {
    return json{
        {"activated_at", draft.activated_at},
        {"authority_status", authority_status_wire(draft.authority_status)},
        {"decision_trace_sha256", draft.decision_trace_sha256},
        {"deployment_id", draft.deployment_id},
        {"expires_at", draft.expires_at},
        {"generation", draft.generation},
        {"previous_root_sha256", draft.previous_root_sha256.has_value()
                                     ? json(*draft.previous_root_sha256)
                                     : json(nullptr)},
        {"schema", schema_document(draft.schema)},
        {"selected_method_sha256", draft.selected_method_sha256},
        {"selected_overlay_sequence", draft.selected_overlay_sequence},
        {"selected_overlay_sha256", draft.selected_overlay_sha256},
        {"selected_selector_sha256", draft.selected_selector_sha256},
        {"sequence_high_water", draft.sequence_high_water},
        {"transition", root_transition_wire(draft.transition)},
    };
}

struct SealedRootData {
    OverlayActivationRootDraft draft;
    std::string checksum;
    std::string canonical;
};

SealedRootData seal_root_data(OverlayActivationRootDraft draft) {
    normalize_root_draft(draft);
    const auto payload = root_payload(draft);
    const auto payload_bytes = payload.dump();
    const auto checksum =
        sha256_hex(std::string_view(activation_root_domain,
                                    sizeof(activation_root_domain) - 1),
                   payload_bytes);
    auto canonical = canonical_document(payload, checksum);
    if (canonical.size() > max_local_overlay_input_bytes) {
        reject(OverlayContractStatus::LimitExceeded,
               "sealed activation root exceeds the input limit");
    }
    return SealedRootData{std::move(draft), checksum, std::move(canonical)};
}

struct ParsedRootDocument {
    OverlayActivationRootDraft draft;
    std::string checksum;
};

ParsedRootDocument parse_root_document(const json &document) {
    require_exact_keys(document,
                       {"activated_at", "authority_status", "checksum_sha256",
                        "decision_trace_sha256", "deployment_id", "expires_at",
                        "generation", "previous_root_sha256", "schema",
                        "selected_method_sha256", "selected_overlay_sequence",
                        "selected_overlay_sha256", "selected_selector_sha256",
                        "sequence_high_water", "transition"},
                       "activation root");
    OverlayActivationRootDraft draft;
    draft.schema = parse_schema(required(document, "schema"));
    draft.deployment_id =
        require_string(required(document, "deployment_id"), "deployment ID");
    draft.generation = require_u64(required(document, "generation"),
                                   "activation-root generation");
    const auto &previous_value = required(document, "previous_root_sha256");
    if (!previous_value.is_null()) {
        draft.previous_root_sha256 =
            require_string(previous_value, "previous activation-root digest");
    }
    draft.transition = parse_root_transition(require_string(
        required(document, "transition"), "activation-root transition"));
    draft.authority_status = parse_authority_status(require_string(
        required(document, "authority_status"), "activation-root status"));
    draft.selected_overlay_sha256 =
        require_string(required(document, "selected_overlay_sha256"),
                       "selected overlay digest");
    draft.selected_overlay_sequence =
        require_u64(required(document, "selected_overlay_sequence"),
                    "selected overlay sequence");
    draft.sequence_high_water =
        require_u64(required(document, "sequence_high_water"),
                    "overlay sequence high-water");
    draft.selected_selector_sha256 =
        require_string(required(document, "selected_selector_sha256"),
                       "selected selector digest");
    draft.selected_method_sha256 = require_string(
        required(document, "selected_method_sha256"), "selected method digest");
    draft.decision_trace_sha256 = require_string(
        required(document, "decision_trace_sha256"), "decision trace digest");
    draft.activated_at = require_string(required(document, "activated_at"),
                                        "activation timestamp");
    draft.expires_at =
        require_string(required(document, "expires_at"), "expiry timestamp");
    return ParsedRootDocument{
        std::move(draft),
        require_string(required(document, "checksum_sha256"),
                       "activation-root checksum"),
    };
}

ParsedProfilingInputEnvelopeResult
rejected_profiling(const OverlayFailure &failure) {
    return ParsedProfilingInputEnvelopeResult{
        failure.status(), bounded_diagnostic(failure.what()), std::nullopt};
}

ParsedProfilingPhaseAttestationResult
rejected_profiling_phase(const OverlayFailure &failure) {
    return ParsedProfilingPhaseAttestationResult{
        failure.status(), bounded_diagnostic(failure.what()), std::nullopt};
}

ParsedLocalOverlayObjectResult rejected_overlay(const OverlayFailure &failure) {
    return ParsedLocalOverlayObjectResult{
        failure.status(), bounded_diagnostic(failure.what()), std::nullopt};
}

ParsedOverlayActivationRootResult rejected_root(const OverlayFailure &failure) {
    return ParsedOverlayActivationRootResult{
        failure.status(), bounded_diagnostic(failure.what()), std::nullopt};
}

} // namespace

ParsedProfilingInputEnvelope::ParsedProfilingInputEnvelope(
    ProfilingInputEnvelopeDraft draft, std::string selector_sha256,
    std::string checksum_sha256, std::string canonical_bytes)
    : draft_(std::move(draft)), selector_sha256_(std::move(selector_sha256)),
      checksum_sha256_(std::move(checksum_sha256)),
      canonical_bytes_(std::move(canonical_bytes)) {}

SchemaVersion ParsedProfilingInputEnvelope::schema() const noexcept {
    return draft_.schema;
}

std::string_view ParsedProfilingInputEnvelope::deployment_id() const noexcept {
    return draft_.deployment_id;
}

std::uint64_t ParsedProfilingInputEnvelope::sequence() const noexcept {
    return draft_.sequence;
}

std::string_view
ParsedProfilingInputEnvelope::profiling_transaction_id() const noexcept {
    return draft_.profiling_transaction_id;
}

const LocalOverlaySelectorIdentity &
ParsedProfilingInputEnvelope::selector() const noexcept {
    return draft_.selector;
}

std::string_view
ParsedProfilingInputEnvelope::selector_sha256() const noexcept {
    return selector_sha256_;
}

const OverlaySourceGenerations &
ParsedProfilingInputEnvelope::generations() const noexcept {
    return draft_.generations;
}

const std::vector<ClaimFamilyClosure> &
ParsedProfilingInputEnvelope::attributed_claims() const noexcept {
    return draft_.attributed_claims;
}

std::string_view
ParsedProfilingInputEnvelope::baseline_observation_sha256() const noexcept {
    return draft_.baseline_observation_sha256;
}

std::string_view
ParsedProfilingInputEnvelope::workload_observation_sha256() const noexcept {
    return draft_.workload_observation_sha256;
}

std::string_view
ParsedProfilingInputEnvelope::release_observation_sha256() const noexcept {
    return draft_.release_observation_sha256;
}

std::string_view
ParsedProfilingInputEnvelope::observation_contract_sha256() const noexcept {
    return draft_.observation_contract_sha256;
}

std::string_view
ParsedProfilingInputEnvelope::predictor_contract_sha256() const noexcept {
    return draft_.predictor_contract_sha256;
}

std::string_view ParsedProfilingInputEnvelope::observed_at() const noexcept {
    return draft_.observed_at;
}

std::string_view ParsedProfilingInputEnvelope::fresh_until() const noexcept {
    return draft_.fresh_until;
}

std::uint64_t
ParsedProfilingInputEnvelope::max_clock_skew_milliseconds() const noexcept {
    return draft_.max_clock_skew_milliseconds;
}

bool ParsedProfilingInputEnvelope::attribution_complete() const noexcept {
    return draft_.attribution_complete;
}

bool ParsedProfilingInputEnvelope::external_demand_absent() const noexcept {
    return draft_.external_demand_absent;
}

bool ParsedProfilingInputEnvelope::lifecycle_release_verified() const noexcept {
    return draft_.lifecycle_release_verified;
}

std::string_view
ParsedProfilingInputEnvelope::checksum_sha256() const noexcept {
    return checksum_sha256_;
}

std::string_view
ParsedProfilingInputEnvelope::canonical_bytes() const noexcept {
    return canonical_bytes_;
}

ParsedProfilingPhaseAttestation::ParsedProfilingPhaseAttestation(
    ProfilingPhaseAttestationDraft draft, std::string checksum_sha256,
    std::string canonical_bytes)
    : draft_(std::move(draft)), checksum_sha256_(std::move(checksum_sha256)),
      canonical_bytes_(std::move(canonical_bytes)) {}

SchemaVersion ParsedProfilingPhaseAttestation::schema() const noexcept {
    return draft_.schema;
}

ProfilingPhase ParsedProfilingPhaseAttestation::phase() const noexcept {
    return draft_.phase;
}

std::string_view
ParsedProfilingPhaseAttestation::deployment_id() const noexcept {
    return draft_.deployment_id;
}

std::string_view
ParsedProfilingPhaseAttestation::profiling_transaction_id() const noexcept {
    return draft_.profiling_transaction_id;
}

std::string_view
ParsedProfilingPhaseAttestation::selector_sha256() const noexcept {
    return draft_.selector_sha256;
}

std::string_view
ParsedProfilingPhaseAttestation::provider_id() const noexcept {
    return draft_.provider_id;
}

std::string_view
ParsedProfilingPhaseAttestation::provider_revision_sha256() const noexcept {
    return draft_.provider_revision_sha256;
}

std::string_view
ParsedProfilingPhaseAttestation::provenance_sha256() const noexcept {
    return draft_.provenance_sha256;
}

std::string_view ParsedProfilingPhaseAttestation::observation_contract_sha256()
    const noexcept {
    return draft_.observation_contract_sha256;
}

std::string_view ParsedProfilingPhaseAttestation::predictor_contract_sha256()
    const noexcept {
    return draft_.predictor_contract_sha256;
}

const OverlaySourceGenerations &
ParsedProfilingPhaseAttestation::generations() const noexcept {
    return draft_.generations;
}

std::uint64_t
ParsedProfilingPhaseAttestation::observation_generation() const noexcept {
    return draft_.observation_generation;
}

std::string_view
ParsedProfilingPhaseAttestation::observed_at() const noexcept {
    return draft_.observed_at;
}

std::string_view
ParsedProfilingPhaseAttestation::fresh_until() const noexcept {
    return draft_.fresh_until;
}

std::uint64_t
ParsedProfilingPhaseAttestation::source_skew_milliseconds() const noexcept {
    return draft_.source_skew_milliseconds;
}

std::uint64_t ParsedProfilingPhaseAttestation::max_source_skew_milliseconds()
    const noexcept {
    return draft_.max_source_skew_milliseconds;
}

ProfilingObservationHealth
ParsedProfilingPhaseAttestation::health() const noexcept {
    return draft_.health;
}

ProfilingOwnerCoverage
ParsedProfilingPhaseAttestation::owner_coverage() const noexcept {
    return draft_.owner_coverage;
}

const std::vector<ClaimFamilyClosure> &
ParsedProfilingPhaseAttestation::observed_claims() const noexcept {
    return draft_.observed_claims;
}

const std::vector<ClaimFamilyClosure> &
ParsedProfilingPhaseAttestation::attributed_claims() const noexcept {
    return draft_.attributed_claims;
}

const std::vector<ClaimFamilyClosure> &
ParsedProfilingPhaseAttestation::external_change_claims() const noexcept {
    return draft_.external_change_claims;
}

const std::vector<ClaimFamilyClosure> &
ParsedProfilingPhaseAttestation::unattributed_claims() const noexcept {
    return draft_.unattributed_claims;
}

const std::vector<ClaimFamilyClosure> &
ParsedProfilingPhaseAttestation::uncertainty_claims() const noexcept {
    return draft_.uncertainty_claims;
}

const std::vector<ClaimFamilyClosure> &
ParsedProfilingPhaseAttestation::safety_margin_claims() const noexcept {
    return draft_.safety_margin_claims;
}

ProfilingLifecycleState
ParsedProfilingPhaseAttestation::lifecycle_state() const noexcept {
    return draft_.lifecycle_state;
}

std::string_view
ParsedProfilingPhaseAttestation::checksum_sha256() const noexcept {
    return checksum_sha256_;
}

std::string_view
ParsedProfilingPhaseAttestation::canonical_bytes() const noexcept {
    return canonical_bytes_;
}

ParsedLocalOverlayObject::ParsedLocalOverlayObject(
    LocalOverlayObjectDraft draft,
    std::vector<ClaimFamilyClosure> conservative_claims,
    std::string selector_sha256, std::string method_sha256,
    std::string checksum_sha256, std::string canonical_bytes)
    : draft_(std::move(draft)),
      conservative_claims_(std::move(conservative_claims)),
      selector_sha256_(std::move(selector_sha256)),
      method_sha256_(std::move(method_sha256)),
      checksum_sha256_(std::move(checksum_sha256)),
      canonical_bytes_(std::move(canonical_bytes)) {}

SchemaVersion ParsedLocalOverlayObject::schema() const noexcept {
    return draft_.schema;
}

std::string_view ParsedLocalOverlayObject::deployment_id() const noexcept {
    return draft_.deployment_id;
}

std::uint64_t ParsedLocalOverlayObject::sequence() const noexcept {
    return draft_.sequence;
}

std::string_view
ParsedLocalOverlayObject::profiling_input_sha256() const noexcept {
    return draft_.profiling_input_sha256;
}

const LocalOverlaySelectorIdentity &
ParsedLocalOverlayObject::selector() const noexcept {
    return draft_.selector;
}

std::string_view ParsedLocalOverlayObject::selector_sha256() const noexcept {
    return selector_sha256_;
}

const LocalOverlayMethodIdentity &
ParsedLocalOverlayObject::method() const noexcept {
    return draft_.method;
}

std::string_view ParsedLocalOverlayObject::method_sha256() const noexcept {
    return method_sha256_;
}

const std::vector<ClaimFamilyClosure> &
ParsedLocalOverlayObject::bound_claims() const noexcept {
    return draft_.bound_claims;
}

const std::vector<ClaimFamilyClosure> &
ParsedLocalOverlayObject::uncertainty_claims() const noexcept {
    return draft_.uncertainty_claims;
}

const std::vector<ClaimFamilyClosure> &
ParsedLocalOverlayObject::safety_margin_claims() const noexcept {
    return draft_.safety_margin_claims;
}

const std::vector<ClaimFamilyClosure> &
ParsedLocalOverlayObject::conservative_claims() const noexcept {
    return conservative_claims_;
}

std::uint32_t
ParsedLocalOverlayObject::confidence_basis_points() const noexcept {
    return draft_.confidence_basis_points;
}

LocalOverlayObjectStatus ParsedLocalOverlayObject::status() const noexcept {
    return draft_.status;
}

std::string_view ParsedLocalOverlayObject::qualified_at() const noexcept {
    return draft_.qualified_at;
}

std::string_view ParsedLocalOverlayObject::expires_at() const noexcept {
    return draft_.expires_at;
}

std::string_view
ParsedLocalOverlayObject::decision_trace_sha256() const noexcept {
    return draft_.decision_trace_sha256;
}

std::string_view ParsedLocalOverlayObject::checksum_sha256() const noexcept {
    return checksum_sha256_;
}

std::string_view ParsedLocalOverlayObject::canonical_bytes() const noexcept {
    return canonical_bytes_;
}

ParsedOverlayActivationRoot::ParsedOverlayActivationRoot(
    OverlayActivationRootDraft draft, std::string checksum_sha256,
    std::string canonical_bytes)
    : draft_(std::move(draft)), checksum_sha256_(std::move(checksum_sha256)),
      canonical_bytes_(std::move(canonical_bytes)) {}

SchemaVersion ParsedOverlayActivationRoot::schema() const noexcept {
    return draft_.schema;
}

std::string_view ParsedOverlayActivationRoot::deployment_id() const noexcept {
    return draft_.deployment_id;
}

std::uint64_t ParsedOverlayActivationRoot::generation() const noexcept {
    return draft_.generation;
}

const std::optional<std::string> &
ParsedOverlayActivationRoot::previous_root_sha256() const noexcept {
    return draft_.previous_root_sha256;
}

OverlayRootTransition ParsedOverlayActivationRoot::transition() const noexcept {
    return draft_.transition;
}

OverlayAuthorityStatus
ParsedOverlayActivationRoot::authority_status() const noexcept {
    return draft_.authority_status;
}

std::string_view
ParsedOverlayActivationRoot::selected_overlay_sha256() const noexcept {
    return draft_.selected_overlay_sha256;
}

std::uint64_t
ParsedOverlayActivationRoot::selected_overlay_sequence() const noexcept {
    return draft_.selected_overlay_sequence;
}

std::uint64_t
ParsedOverlayActivationRoot::sequence_high_water() const noexcept {
    return draft_.sequence_high_water;
}

std::string_view
ParsedOverlayActivationRoot::selected_selector_sha256() const noexcept {
    return draft_.selected_selector_sha256;
}

std::string_view
ParsedOverlayActivationRoot::selected_method_sha256() const noexcept {
    return draft_.selected_method_sha256;
}

std::string_view
ParsedOverlayActivationRoot::decision_trace_sha256() const noexcept {
    return draft_.decision_trace_sha256;
}

std::string_view ParsedOverlayActivationRoot::activated_at() const noexcept {
    return draft_.activated_at;
}

std::string_view ParsedOverlayActivationRoot::expires_at() const noexcept {
    return draft_.expires_at;
}

std::string_view ParsedOverlayActivationRoot::checksum_sha256() const noexcept {
    return checksum_sha256_;
}

std::string_view ParsedOverlayActivationRoot::canonical_bytes() const noexcept {
    return canonical_bytes_;
}

bool ParsedProfilingInputEnvelopeResult::accepted() const noexcept {
    return status == OverlayContractStatus::Accepted && candidate.has_value();
}

bool ParsedProfilingPhaseAttestationResult::accepted() const noexcept {
    return status == OverlayContractStatus::Accepted && candidate.has_value();
}

bool ParsedLocalOverlayObjectResult::accepted() const noexcept {
    return status == OverlayContractStatus::Accepted && candidate.has_value();
}

bool ParsedOverlayActivationRootResult::accepted() const noexcept {
    return status == OverlayContractStatus::Accepted && candidate.has_value();
}

ParsedProfilingInputEnvelopeResult
seal_profiling_input(ProfilingInputEnvelopeDraft draft) {
    try {
        auto sealed = seal_profiling_data(std::move(draft));
        return ParsedProfilingInputEnvelopeResult{
            OverlayContractStatus::Accepted,
            {},
            ParsedProfilingInputEnvelope(
                std::move(sealed.draft), std::move(sealed.selector_sha256),
                std::move(sealed.checksum), std::move(sealed.canonical)),
        };
    } catch (const OverlayFailure &failure) {
        return rejected_profiling(failure);
    }
}

ParsedProfilingInputEnvelopeResult
parse_profiling_input(std::string_view bytes) {
    try {
        auto parsed = parse_profiling_document(parse_json(bytes));
        require_digest(parsed.checksum, "profiling input checksum");
        auto sealed = seal_profiling_data(std::move(parsed.draft));
        if (parsed.checksum != sealed.checksum) {
            reject(OverlayContractStatus::DigestMismatch,
                   "profiling input checksum does not match");
        }
        if (bytes != sealed.canonical) {
            reject(OverlayContractStatus::NonCanonical,
                   "profiling input is not canonical JSON");
        }
        return ParsedProfilingInputEnvelopeResult{
            OverlayContractStatus::Accepted,
            {},
            ParsedProfilingInputEnvelope(
                std::move(sealed.draft), std::move(sealed.selector_sha256),
                std::move(sealed.checksum), std::move(sealed.canonical)),
        };
    } catch (const OverlayFailure &failure) {
        return rejected_profiling(failure);
    }
}

ParsedProfilingPhaseAttestationResult seal_profiling_phase_attestation(
    ProfilingPhaseAttestationDraft draft) {
    try {
        auto sealed = seal_profiling_phase_data(std::move(draft));
        return ParsedProfilingPhaseAttestationResult{
            OverlayContractStatus::Accepted,
            {},
            ParsedProfilingPhaseAttestation(
                std::move(sealed.draft), std::move(sealed.checksum),
                std::move(sealed.canonical)),
        };
    } catch (const OverlayFailure &failure) {
        return rejected_profiling_phase(failure);
    }
}

ParsedProfilingPhaseAttestationResult
parse_profiling_phase_attestation(std::string_view bytes) {
    try {
        auto parsed = parse_profiling_phase_document(parse_json(bytes));
        require_digest(parsed.checksum,
                       "profiling phase attestation checksum");
        auto sealed = seal_profiling_phase_data(std::move(parsed.draft));
        if (parsed.checksum != sealed.checksum) {
            reject(OverlayContractStatus::DigestMismatch,
                   "profiling phase attestation checksum does not match");
        }
        if (bytes != sealed.canonical) {
            reject(OverlayContractStatus::NonCanonical,
                   "profiling phase attestation is not canonical JSON");
        }
        return ParsedProfilingPhaseAttestationResult{
            OverlayContractStatus::Accepted,
            {},
            ParsedProfilingPhaseAttestation(
                std::move(sealed.draft), std::move(sealed.checksum),
                std::move(sealed.canonical)),
        };
    } catch (const OverlayFailure &failure) {
        return rejected_profiling_phase(failure);
    }
}

ParsedLocalOverlayObjectResult
seal_local_overlay(LocalOverlayObjectDraft draft) {
    try {
        auto sealed = seal_overlay_data(std::move(draft));
        return ParsedLocalOverlayObjectResult{
            OverlayContractStatus::Accepted,
            {},
            ParsedLocalOverlayObject(
                std::move(sealed.draft), std::move(sealed.conservative_claims),
                std::move(sealed.selector_sha256),
                std::move(sealed.method_sha256), std::move(sealed.checksum),
                std::move(sealed.canonical)),
        };
    } catch (const OverlayFailure &failure) {
        return rejected_overlay(failure);
    }
}

ParsedLocalOverlayObjectResult parse_local_overlay(std::string_view bytes) {
    try {
        auto parsed = parse_overlay_document(parse_json(bytes));
        require_digest(parsed.checksum, "local overlay checksum");
        auto sealed = seal_overlay_data(std::move(parsed.draft));
        if (parsed.checksum != sealed.checksum) {
            reject(OverlayContractStatus::DigestMismatch,
                   "local overlay checksum does not match");
        }
        if (bytes != sealed.canonical) {
            reject(OverlayContractStatus::NonCanonical,
                   "local overlay is not canonical JSON");
        }
        return ParsedLocalOverlayObjectResult{
            OverlayContractStatus::Accepted,
            {},
            ParsedLocalOverlayObject(
                std::move(sealed.draft), std::move(sealed.conservative_claims),
                std::move(sealed.selector_sha256),
                std::move(sealed.method_sha256), std::move(sealed.checksum),
                std::move(sealed.canonical)),
        };
    } catch (const OverlayFailure &failure) {
        return rejected_overlay(failure);
    }
}

ParsedOverlayActivationRootResult
seal_overlay_activation_root(OverlayActivationRootDraft draft) {
    try {
        auto sealed = seal_root_data(std::move(draft));
        return ParsedOverlayActivationRootResult{
            OverlayContractStatus::Accepted,
            {},
            ParsedOverlayActivationRoot(std::move(sealed.draft),
                                        std::move(sealed.checksum),
                                        std::move(sealed.canonical)),
        };
    } catch (const OverlayFailure &failure) {
        return rejected_root(failure);
    }
}

ParsedOverlayActivationRootResult
parse_overlay_activation_root(std::string_view bytes) {
    try {
        auto parsed = parse_root_document(parse_json(bytes));
        require_digest(parsed.checksum, "activation-root checksum");
        auto sealed = seal_root_data(std::move(parsed.draft));
        if (parsed.checksum != sealed.checksum) {
            reject(OverlayContractStatus::DigestMismatch,
                   "activation-root checksum does not match");
        }
        if (bytes != sealed.canonical) {
            reject(OverlayContractStatus::NonCanonical,
                   "activation root is not canonical JSON");
        }
        return ParsedOverlayActivationRootResult{
            OverlayContractStatus::Accepted,
            {},
            ParsedOverlayActivationRoot(std::move(sealed.draft),
                                        std::move(sealed.checksum),
                                        std::move(sealed.canonical)),
        };
    } catch (const OverlayFailure &failure) {
        return rejected_root(failure);
    }
}

} // namespace lemon::residency
