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
namespace {

bool hashing_is_available() noexcept {
#if MBEDTLS_VERSION_MAJOR >= 4
    static std::once_flag initialized;
    static psa_status_t initialization_status = PSA_ERROR_BAD_STATE;
    std::call_once(initialized,
                   [] { initialization_status = psa_crypto_init(); });
    if (initialization_status != PSA_SUCCESS) return false;
#endif
    return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256) != nullptr;
}

std::string hexadecimal_digest(const std::array<unsigned char, 32> &digest) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) {
        result.push_back(hex[(byte >> 4) & 0x0f]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

} // namespace

class BoundedSha256::Implementation {
public:
    explicit Implementation(std::uint64_t maximum_bytes) noexcept
        : maximum_bytes_(maximum_bytes) {
        mbedtls_md_init(&context_);
        if (!hashing_is_available()) return;
        const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        active_ = mbedtls_md_setup(&context_, info, 0) == 0 &&
                  mbedtls_md_starts(&context_) == 0;
    }

    ~Implementation() { mbedtls_md_free(&context_); }

    bool append(std::string_view bytes) noexcept {
        if (!active_ || finished_ ||
            bytes.size() > maximum_bytes_ - bytes_hashed_) {
            active_ = false;
            return false;
        }
        if (bytes.empty()) return true;
        if (mbedtls_md_update(
                &context_,
                reinterpret_cast<const unsigned char *>(bytes.data()),
                bytes.size()) != 0) {
            active_ = false;
            return false;
        }
        bytes_hashed_ += static_cast<std::uint64_t>(bytes.size());
        return true;
    }

    bool append_u64(std::uint64_t value) noexcept {
        std::array<char, 8> encoded{};
        for (std::size_t index = 0; index < encoded.size(); ++index) {
            const auto shift = 56 - static_cast<int>(index * 8);
            encoded[index] =
                static_cast<char>((value >> shift) & 0xffu);
        }
        return append(std::string_view(encoded.data(), encoded.size()));
    }

    bool append_string(std::string_view value) noexcept {
        const auto remaining = maximum_bytes_ - bytes_hashed_;
        if (remaining < 8 || value.size() > remaining - 8) {
            active_ = false;
            return false;
        }
        return append_u64(static_cast<std::uint64_t>(value.size())) &&
               append(value);
    }

    std::optional<std::string> finish() noexcept {
        if (!active_ || finished_) return std::nullopt;
        std::array<unsigned char, 32> digest{};
        if (mbedtls_md_finish(&context_, digest.data()) != 0) {
            active_ = false;
            return std::nullopt;
        }
        finished_ = true;
        try {
            return hexadecimal_digest(digest);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::uint64_t bytes_hashed() const noexcept { return bytes_hashed_; }

private:
    mbedtls_md_context_t context_;
    std::uint64_t maximum_bytes_ = 0;
    std::uint64_t bytes_hashed_ = 0;
    bool active_ = false;
    bool finished_ = false;
};

BoundedSha256::BoundedSha256(std::uint64_t maximum_bytes) noexcept {
    try {
        implementation_ = std::make_unique<Implementation>(maximum_bytes);
    } catch (...) {
    }
}

BoundedSha256::~BoundedSha256() = default;

bool BoundedSha256::append(std::string_view bytes) noexcept {
    return implementation_ && implementation_->append(bytes);
}

bool BoundedSha256::append_u64(std::uint64_t value) noexcept {
    return implementation_ && implementation_->append_u64(value);
}

bool BoundedSha256::append_string(std::string_view value) noexcept {
    return implementation_ && implementation_->append_string(value);
}

std::optional<std::string> BoundedSha256::finish() noexcept {
    if (!implementation_) return std::nullopt;
    return implementation_->finish();
}

std::uint64_t BoundedSha256::bytes_hashed() const noexcept {
    return implementation_ ? implementation_->bytes_hashed() : 0;
}

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
    BoundedSha256 stream(static_cast<std::uint64_t>(bytes.size()));
    if (!stream.append(bytes)) return std::nullopt;
    return stream.finish();
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
