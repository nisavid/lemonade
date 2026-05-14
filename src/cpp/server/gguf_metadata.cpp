#include "lemon/gguf_metadata.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

namespace lemon {
namespace {

template <typename T>
bool read_le(std::istream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

bool read_gguf_string(std::istream& in, std::string& value) {
    uint64_t len = 0;
    if (!read_le(in, len)) return false;
    if (len > 1024 * 1024) return false;
    value.assign(static_cast<size_t>(len), '\0');
    if (len == 0) return true;
    in.read(&value[0], static_cast<std::streamsize>(len));
    return static_cast<bool>(in);
}

bool skip_bytes(std::istream& in, uint64_t bytes) {
    if (bytes > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) return false;
    in.seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
    return static_cast<bool>(in);
}

uint64_t gguf_scalar_size(uint32_t type) {
    switch (type) {
        case 0:
        case 1:
        case 7:
            return 1;
        case 2:
        case 3:
            return 2;
        case 4:
        case 5:
        case 6:
            return 4;
        case 10:
        case 11:
        case 12:
            return 8;
        default:
            return 0;
    }
}

bool skip_gguf_value(std::istream& in, uint32_t type);

bool read_gguf_integer_value(std::istream& in, uint32_t type, int64_t& value) {
    switch (type) {
        case 0: { uint8_t v = 0; if (!read_le(in, v)) return false; value = v; return true; }
        case 1: { int8_t v = 0; if (!read_le(in, v)) return false; value = v; return true; }
        case 2: { uint16_t v = 0; if (!read_le(in, v)) return false; value = v; return true; }
        case 3: { int16_t v = 0; if (!read_le(in, v)) return false; value = v; return true; }
        case 4: { uint32_t v = 0; if (!read_le(in, v)) return false; value = v; return true; }
        case 5: { int32_t v = 0; if (!read_le(in, v)) return false; value = v; return true; }
        case 10: {
            uint64_t v = 0;
            if (!read_le(in, v)) return false;
            if (v > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return false;
            value = static_cast<int64_t>(v);
            return true;
        }
        case 11: { int64_t v = 0; if (!read_le(in, v)) return false; value = v; return true; }
        default:
            return false;
    }
}

bool skip_gguf_value(std::istream& in, uint32_t type) {
    if (type == 8) {
        std::string ignored;
        return read_gguf_string(in, ignored);
    }

    if (type == 9) {
        uint32_t elem_type = 0;
        uint64_t count = 0;
        if (!read_le(in, elem_type) || !read_le(in, count)) return false;

        if (elem_type == 8) {
            for (uint64_t i = 0; i < count; ++i) {
                std::string ignored;
                if (!read_gguf_string(in, ignored)) return false;
            }
            return true;
        }

        if (elem_type == 9) return false;
        uint64_t elem_size = gguf_scalar_size(elem_type);
        if (elem_size == 0) return false;
        if (count > std::numeric_limits<uint64_t>::max() / elem_size) return false;
        return skip_bytes(in, count * elem_size);
    }

    uint64_t size = gguf_scalar_size(type);
    return size > 0 && skip_bytes(in, size);
}

bool set_integer_field(GgufMetadata& metadata, const std::string& suffix, int64_t value) {
    if (value <= 0) return true;
    if (suffix == ".context_length") metadata.context_length = value;
    else if (suffix == ".block_count") metadata.block_count = value;
    else if (suffix == ".embedding_length") metadata.embedding_length = value;
    else if (suffix == ".attention.head_count") metadata.head_count = value;
    else if (suffix == ".attention.head_count_kv") metadata.head_count_kv = value;
    else if (suffix == ".attention.key_length") metadata.key_length = value;
    else if (suffix == ".attention.value_length") metadata.value_length = value;
    else if (suffix == ".attention.sliding_window") metadata.sliding_window = value;
    else return false;
    return true;
}

std::string strip_arch_prefix(const std::string& key, const std::string& architecture) {
    if (!architecture.empty()) {
        const std::string prefix = architecture + ".";
        if (key.rfind(prefix, 0) == 0) {
            return "." + key.substr(prefix.size());
        }
    }

    const size_t first_dot = key.find('.');
    if (first_dot == std::string::npos) return "";
    return key.substr(first_dot);
}

} // namespace

std::optional<GgufMetadata> read_gguf_metadata(const std::string& path) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) return std::nullopt;

    char magic[4] = {};
    in.read(magic, sizeof(magic));
    if (!in || std::memcmp(magic, "GGUF", 4) != 0) return std::nullopt;

    uint32_t version = 0;
    uint64_t tensor_count = 0;
    uint64_t kv_count = 0;
    if (!read_le(in, version) || !read_le(in, tensor_count) || !read_le(in, kv_count)) {
        return std::nullopt;
    }
    (void)version;
    (void)tensor_count;

    GgufMetadata metadata;

    for (uint64_t i = 0; i < kv_count; ++i) {
        std::string key;
        uint32_t type = 0;
        if (!read_gguf_string(in, key) || !read_le(in, type)) return std::nullopt;

        if (key == "general.architecture" && type == 8) {
            if (!read_gguf_string(in, metadata.architecture)) return std::nullopt;
            continue;
        }

        const std::string suffix = strip_arch_prefix(key, metadata.architecture);
        if (!suffix.empty()) {
            int64_t value = 0;
            if (read_gguf_integer_value(in, type, value)) {
                set_integer_field(metadata, suffix, value);
                continue;
            } else {
                if (!skip_gguf_value(in, type)) return std::nullopt;
                continue;
            }
        }

        if (!skip_gguf_value(in, type)) return std::nullopt;
    }

    return metadata;
}

} // namespace lemon
