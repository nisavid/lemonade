#pragma once

#include "lemon/residency/types.h"

#include <string_view>

namespace lemon::residency {

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
    static ReasonCode decode_reason_code(std::string_view wire);
    static DecodedValue<FallbackId> decode_fallback_id(std::string_view wire);
    static DecodedValue<SchemaType> decode_schema_type(std::string_view wire);
    static const ReasonMetadata* reason_metadata(std::string_view code) noexcept;
};

DecodedValue<PromotionUnitId> decode_promotion_unit_id(std::string_view wire);
ReasonCode decode_reason_code(std::string_view wire);
DecodedValue<FallbackId> decode_fallback_id(std::string_view wire);
DecodedValue<SchemaType> decode_schema_type(std::string_view wire);
const ReasonMetadata* reason_metadata(std::string_view code) noexcept;
const ReasonMetadata* reason_metadata(const KnownReasonCode& code) noexcept;
const ReasonMetadata* reason_metadata(const ReasonCode& code) noexcept;

}
