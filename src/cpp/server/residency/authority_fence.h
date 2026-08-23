#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lemon::residency::detail {

struct IdentityFenceEpoch {};

using IdentityFenceToken = std::shared_ptr<const IdentityFenceEpoch>;

inline std::mutex fenced_identities_mutex;
inline std::unordered_map<std::string, IdentityFenceToken> fenced_identities;

inline std::string_view directory_identity(std::string_view identity) noexcept {
    const auto delimiter = identity.rfind("/lock:");
    if (delimiter == std::string_view::npos) {
        return identity;
    }
    return identity.substr(0, delimiter);
}

inline bool same_directory_identity(std::string_view left,
                                    std::string_view right) noexcept {
    return directory_identity(left) == directory_identity(right);
}

inline IdentityFenceToken fence_identity(std::string_view identity) {
    if (identity.empty()) {
        return nullptr;
    }
    auto token = std::make_shared<IdentityFenceEpoch>();
    std::lock_guard lock(fenced_identities_mutex);
    fenced_identities.insert_or_assign(
        std::string(directory_identity(identity)), token);
    return token;
}

inline IdentityFenceToken identity_fence(std::string_view identity) {
    std::lock_guard lock(fenced_identities_mutex);
    const auto found =
        fenced_identities.find(std::string(directory_identity(identity)));
    return found == fenced_identities.end() ? nullptr : found->second;
}

inline void clear_identity_fence(std::string_view identity,
                                 const IdentityFenceToken &observed) {
    if (!observed) {
        return;
    }
    std::lock_guard lock(fenced_identities_mutex);
    const auto found =
        fenced_identities.find(std::string(directory_identity(identity)));
    if (found != fenced_identities.end() && found->second == observed) {
        fenced_identities.erase(found);
    }
}

inline bool identity_is_fenced(std::string_view identity) {
    return static_cast<bool>(identity_fence(identity));
}

} // namespace lemon::residency::detail
