#pragma once

#include "lemon/residency/types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace lemon::residency {

inline constexpr std::string_view kPackagedCatalogSha256 = "a6dc806b5f69b6c44ec8e373c8880e0ce3f00da4a01a3388d00336d267176e42";
inline constexpr std::string_view kExplanationSchemaId = "residency.explanation/1.0";
inline constexpr SchemaVersion kExplanationSchemaVersion{1, 0};
inline constexpr std::size_t kMaxExplanationReasons = 16;

struct OperationRetentionPolicy {
    bool active_expires;
    bool recovery_required_expires;
    std::uint64_t terminal_detail_seconds;
    std::uint64_t forgotten_after_terminal_seconds;
};

inline constexpr OperationRetentionPolicy kOperationRetentionPolicy{false, false, 86400, 604800};

struct ReasonMetadata {
    std::string_view code;
    std::string_view category_id;
    std::string_view presentation_id;
    std::string_view detail_schema_id;
    std::string_view severity;
    std::string_view title;
    std::string_view default_message;
};

struct ReasonPresentationMetadata {
    std::string_view id;
    std::string_view category_id;
    std::string_view severity;
    std::string_view title;
    std::string_view default_message;
};

struct OperationReasonRuleMetadata {
    std::string_view code;
    std::size_t canonical_rank;
    std::uint32_t priority_band;
    bool secondary_only;
};

class GeneratedContractRegistry {
public:
    static DecodedValue<PromotionUnitId> decode_promotion_unit_id(std::string_view wire);
    static PromotionUnitKind promotion_unit_kind(const PromotionUnitId& id) noexcept;
    static ReasonCode decode_reason_code(std::string_view wire);
    static DecodedValue<FallbackId> decode_fallback_id(std::string_view wire);
    static DecodedValue<SchemaType> decode_schema_type(std::string_view wire);
    static const ReasonMetadata* reason_metadata(std::string_view code) noexcept;
};

DecodedValue<PromotionUnitId> decode_promotion_unit_id(std::string_view wire);
PromotionUnitKind promotion_unit_kind(const PromotionUnitId& id) noexcept;
ReasonCode decode_reason_code(std::string_view wire);
DecodedValue<FallbackId> decode_fallback_id(std::string_view wire);
DecodedValue<SchemaType> decode_schema_type(std::string_view wire);
const ReasonMetadata* reason_metadata(std::string_view code) noexcept;
const ReasonMetadata* reason_metadata(const KnownReasonCode& code) noexcept;
const ReasonMetadata* reason_metadata(const ReasonCode& code) noexcept;
OperationFamily operation_family(OperationKind kind) noexcept;
const OperationReasonRuleMetadata* operation_reason_rule_metadata(std::string_view code) noexcept;
bool operation_reason_is_legal(std::string_view code, OperationKind kind, OperationPhase phase, std::optional<TerminalOutcome> terminal_outcome, bool secondary) noexcept;
const ReasonPresentationMetadata* reason_presentation_metadata(std::string_view presentation_id) noexcept;
bool reason_category_is_known(std::string_view category_id) noexcept;
const ReasonPresentationMetadata* unique_reason_presentation_for_category(std::string_view category_id) noexcept;
const ReasonPresentationMetadata* matching_reason_presentation_for_category(std::string_view category_id, std::string_view presentation_id) noexcept;

}
