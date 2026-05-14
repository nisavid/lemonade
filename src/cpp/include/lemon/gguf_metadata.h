#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace lemon {

struct GgufMetadata {
    std::string architecture;
    int64_t context_length = 0;
    int64_t block_count = 0;
    int64_t embedding_length = 0;
    int64_t head_count = 0;
    int64_t head_count_kv = 0;
    int64_t key_length = 0;
    int64_t value_length = 0;
    int64_t sliding_window = 0;
};

std::optional<GgufMetadata> read_gguf_metadata(const std::string& path);

} // namespace lemon
