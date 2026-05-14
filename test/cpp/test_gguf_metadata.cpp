#include "lemon/gguf_metadata.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

using lemon::read_gguf_metadata;

template <typename T>
static void write_le(std::ofstream& out, T value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

static void write_string(std::ofstream& out, const std::string& value) {
    write_le<uint64_t>(out, value.size());
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
}

static void write_string_kv(std::ofstream& out, const std::string& key, const std::string& value) {
    write_string(out, key);
    write_le<uint32_t>(out, 8);
    write_string(out, value);
}

static void write_u32_kv(std::ofstream& out, const std::string& key, uint32_t value) {
    write_string(out, key);
    write_le<uint32_t>(out, 4);
    write_le<uint32_t>(out, value);
}

int main() {
    const std::string path = "/tmp/lemonade-test-metadata.gguf";
    {
        std::ofstream out(path, std::ios::binary);
        out.write("GGUF", 4);
        write_le<uint32_t>(out, 3);
        write_le<uint64_t>(out, 0);
        write_le<uint64_t>(out, 10);
        write_string_kv(out, "general.architecture", "qwen3");
        write_u32_kv(out, "qwen3.unknown_integer_field", 1234);
        write_u32_kv(out, "qwen3.context_length", 262144);
        write_u32_kv(out, "qwen3.block_count", 80);
        write_u32_kv(out, "qwen3.embedding_length", 8192);
        write_u32_kv(out, "qwen3.attention.head_count", 64);
        write_u32_kv(out, "qwen3.attention.head_count_kv", 8);
        write_u32_kv(out, "qwen3.attention.key_length", 128);
        write_u32_kv(out, "qwen3.attention.value_length", 128);
        write_u32_kv(out, "qwen3.attention.sliding_window", 32768);
    }

    auto metadata = read_gguf_metadata(path);
    assert(metadata.has_value());
    assert(metadata->architecture == "qwen3");
    assert(metadata->context_length == 262144);
    assert(metadata->block_count == 80);
    assert(metadata->embedding_length == 8192);
    assert(metadata->head_count == 64);
    assert(metadata->head_count_kv == 8);
    assert(metadata->key_length == 128);
    assert(metadata->value_length == 128);
    assert(metadata->sliding_window == 32768);

    std::printf("GGUF metadata parsing passed\n");
    return 0;
}
