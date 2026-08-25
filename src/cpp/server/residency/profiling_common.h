#pragma once

#include "lemon/residency/profiling_transaction.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lemon::residency::profiling_internal {

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
