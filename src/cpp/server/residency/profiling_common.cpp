#include "profiling_common.h"

#include <mbedtls/md.h>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_MAJOR >= 4
#include <psa/crypto.h>
#endif

#include <algorithm>
#include <array>
#include <limits>
#include <mutex>
#include <type_traits>

namespace lemon::residency::profiling_internal {

std::string bounded_diagnostic(std::string value) {
    if (value.size() <= max_local_overlay_diagnostic_bytes) return value;
    std::size_t boundary = max_local_overlay_diagnostic_bytes;
    while (boundary > 0 &&
           (static_cast<unsigned char>(value[boundary]) & 0xc0u) == 0x80u) {
        --boundary;
    }
    value.resize(boundary);
    return value;
}

bool cancelled(const ProfilingCancellationCheck &should_abort) noexcept {
    if (!should_abort) return false;
    try {
        return should_abort();
    } catch (...) {
        return true;
    }
}

bool digest_is_valid(std::string_view value) noexcept {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

void append_u64(std::string &bytes, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<char>((value >> shift) & 0xffu));
    }
}

void append_string(std::string &bytes, std::string_view value) {
    append_u64(bytes, static_cast<std::uint64_t>(value.size()));
    bytes.append(value.data(), value.size());
}

std::optional<std::string> sha256_hex(std::string_view bytes) {
#if MBEDTLS_VERSION_MAJOR >= 4
    static std::once_flag initialized;
    static psa_status_t initialization_status = PSA_ERROR_BAD_STATE;
    std::call_once(initialized,
                   [] { initialization_status = psa_crypto_init(); });
    if (initialization_status != PSA_SUCCESS) return std::nullopt;
#endif

    const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) return std::nullopt;

    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    std::array<unsigned char, 32> digest{};
    const bool failed =
        mbedtls_md_setup(&context, info, 0) != 0 ||
        mbedtls_md_starts(&context) != 0 ||
        mbedtls_md_update(
            &context,
            reinterpret_cast<const unsigned char *>(bytes.data()),
            bytes.size()) != 0 ||
        mbedtls_md_finish(&context, digest.data()) != 0;
    mbedtls_md_free(&context);
    if (failed) return std::nullopt;

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) {
        result.push_back(hex[(byte >> 4) & 0x0f]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

std::optional<std::chrono::steady_clock::duration> elapsed_between(
    std::chrono::steady_clock::time_point started,
    std::chrono::steady_clock::time_point finished) noexcept {
    using Duration = std::chrono::steady_clock::duration;
    using Rep = Duration::rep;
    static_assert(std::is_integral_v<Rep>);

    const auto start = started.time_since_epoch().count();
    const auto finish = finished.time_since_epoch().count();
    if (finish < start) return std::nullopt;

    using UnsignedRep = std::make_unsigned_t<Rep>;
    const auto ticks = static_cast<UnsignedRep>(finish) -
                       static_cast<UnsignedRep>(start);
    if constexpr (std::is_signed_v<Rep>) {
        if (ticks > static_cast<UnsignedRep>(
                        std::numeric_limits<Rep>::max())) {
            return std::nullopt;
        }
    }
    return Duration(static_cast<Rep>(ticks));
}

} // namespace lemon::residency::profiling_internal
