#pragma once

#include <cstddef>

namespace lemon::residency::profiling_internal::test {

enum class Sha256FailureMode {
    ReturnUnavailable,
    ThrowException,
};

void fail_sha256_after(std::size_t successful_calls,
                       Sha256FailureMode mode) noexcept;

} // namespace lemon::residency::profiling_internal::test
