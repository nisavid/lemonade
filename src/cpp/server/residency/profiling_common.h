#pragma once

#include "lemon/residency/profiling_transaction.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lemon::residency::profiling_internal {

class BoundedSha256 {
public:
    explicit BoundedSha256(std::uint64_t maximum_bytes) noexcept;
    ~BoundedSha256();

    BoundedSha256(const BoundedSha256 &) = delete;
    BoundedSha256 &operator=(const BoundedSha256 &) = delete;

    bool append(std::string_view bytes) noexcept;
    bool append_u64(std::uint64_t value) noexcept;
    bool append_string(std::string_view value) noexcept;
    std::optional<std::string> finish() noexcept;
    std::uint64_t bytes_hashed() const noexcept;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

std::string bounded_diagnostic(std::string value);

bool cancelled(const ProfilingCancellationCheck &should_abort) noexcept;

bool digest_is_valid(std::string_view value) noexcept;

void append_u64(std::string &bytes, std::uint64_t value);

void append_string(std::string &bytes, std::string_view value);

std::optional<std::string> sha256_hex(std::string_view bytes);

std::optional<std::chrono::steady_clock::duration> elapsed_between(
    std::chrono::steady_clock::time_point started,
    std::chrono::steady_clock::time_point finished) noexcept;

} // namespace lemon::residency::profiling_internal
