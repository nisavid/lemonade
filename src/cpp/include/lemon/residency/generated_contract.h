#pragma once

#include "lemon/residency/types.h"

#include <string_view>

namespace lemon::residency {

inline constexpr std::string_view kPackagedCatalogSha256 = "a6dc806b5f69b6c44ec8e373c8880e0ce3f00da4a01a3388d00336d267176e42";

struct ReasonMetadata {
    std::string_view code;
    std::string_view category_id;
    std::string_view presentation_id;
    std::string_view detail_schema_id;
    std::string_view severity;
    std::string_view title;
    std::string_view default_message;
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

}
