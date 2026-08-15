#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace lemon {

class ModelLoadTracker {
public:
    void begin(const std::string& model_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++states_[model_name].load_count;
    }

    bool is_loading(const std::string& model_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return states_.find(model_name) != states_.end();
    }

    bool set_pin_override_if_loading(const std::string& model_name, bool pinned) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = states_.find(model_name);
        if (it == states_.end()) {
            return false;
        }
        it->second.pin_override = pinned;
        return true;
    }

    std::optional<bool> finish(const std::string& model_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = states_.find(model_name);
        if (it == states_.end()) {
            return std::nullopt;
        }
        if (--it->second.load_count != 0) {
            return std::nullopt;
        }
        const auto pin_override = it->second.pin_override;
        states_.erase(it);
        return pin_override;
    }

private:
    struct State {
        std::size_t load_count = 0;
        std::optional<bool> pin_override;
    };

    mutable std::mutex mutex_;
    std::map<std::string, State> states_;
};

}  // namespace lemon
