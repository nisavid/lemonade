#include "lemon/gguf_metadata.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using lemon::read_gguf_metadata;

namespace fs = std::filesystem;

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
    int failures = 0;
    auto expect_true = [&failures](bool condition, const char* name) {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
        if (!condition) ++failures;
    };
    auto expect_eq_i64 = [&failures](int64_t actual, int64_t expected, const char* name) {
        bool ok = actual == expected;
        std::printf("[%s] %s  (got=%lld, want=%lld)\n",
                    ok ? "PASS" : "FAIL",
                    name,
                    static_cast<long long>(actual),
                    static_cast<long long>(expected));
        if (!ok) ++failures;
    };
    auto expect_eq_string = [&failures](const std::string& actual,
                                        const std::string& expected,
                                        const char* name) {
        bool ok = actual == expected;
        std::printf("[%s] %s  (got=%s, want=%s)\n",
                    ok ? "PASS" : "FAIL",
                    name,
                    actual.c_str(),
                    expected.c_str());
        if (!ok) ++failures;
    };

    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path path = fs::temp_directory_path() /
        ("lemonade-test-metadata-" + std::to_string(unique_suffix) + ".gguf");
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

    auto metadata = read_gguf_metadata(path.string());
    expect_true(metadata.has_value(), "metadata parsed");
    if (metadata) {
        expect_eq_string(metadata->architecture, "qwen3", "architecture");
        expect_eq_i64(metadata->context_length, 262144, "context_length");
        expect_eq_i64(metadata->block_count, 80, "block_count");
        expect_eq_i64(metadata->embedding_length, 8192, "embedding_length");
        expect_eq_i64(metadata->head_count, 64, "head_count");
        expect_eq_i64(metadata->head_count_kv, 8, "head_count_kv");
        expect_eq_i64(metadata->key_length, 128, "key_length");
        expect_eq_i64(metadata->value_length, 128, "value_length");
        expect_eq_i64(metadata->sliding_window, 32768, "sliding_window");
    }

    std::error_code ec;
    fs::remove(path, ec);

    std::printf("\nGGUF metadata parsing %s\n", failures == 0 ? "passed" : "failed");
    return failures == 0 ? 0 : 1;
}
