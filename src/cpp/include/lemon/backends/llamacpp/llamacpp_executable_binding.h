#pragma once

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace lemon {

class RecipeOptions;

namespace backends {
namespace llamacpp {

struct ExecutableBinding {
    std::string executable;
    std::string backend;
    std::string source;
};

enum class BindingState {
    Bound,     // a valid binding applies
    Unbound,   // config.json unbinds the model with null
    Rejected,  // the model's binding configuration is invalid; its loads fail
};

struct BindingEntry {
    BindingState state = BindingState::Rejected;
    ExecutableBinding binding;
    std::string source;
    std::string error;
};

struct BindingLayer {
    bool present = false;
    nlohmann::json value;
    std::string source;
};

// Raised before admission when a load cannot honor a model's binding.
class ExecutableBindingError : public std::runtime_error {
public:
    explicit ExecutableBindingError(const std::string& message)
        : std::runtime_error(message) {}
};

// The effective binding table, keyed by model cache key: the bare registry
// name for a built-in model, the `user.`/`extra.` ID otherwise. It is read
// once at startup and never changes afterwards.
class ExecutableBindings {
public:
    ExecutableBindings() = default;

    // Fragments only add entries. A config.json entry replaces a fragment's
    // entry whole, and a config.json null unbinds the model. The key is
    // reserved in every defaults layer. Each layer is checked before merging,
    // because a merge drops a non-object section under an object one.
    static ExecutableBindings resolve(const std::vector<BindingLayer>& defaults_layers,
                                      const std::string& fragments_dir,
                                      const BindingLayer& config_layer);

    // Reads the defaults chain and config.json from `config_dir`, plus the
    // fragment directory named by fragments_dir_from_environment().
    static ExecutableBindings load(const std::string& config_dir);

    const BindingEntry* find(const std::string& cache_key) const;
    std::optional<ExecutableBinding> bound(const std::string& cache_key) const;

    // Set when no single model can be blamed (an unreadable fragment directory,
    // a fragment file name that names no model, or a malformed config.json or
    // defaults-layer map); every llama.cpp load then fails.
    const std::string& map_error() const { return map_error_; }
    const std::string& map_error_source() const { return map_error_source_; }

    const std::map<std::string, BindingEntry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty() && map_error_.empty(); }

    // `source` and `error` are output-only fields.
    nlohmann::json to_json() const;

    const std::vector<std::string>& info_messages() const { return info_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    void log_summary() const;

private:
    void add_map_error(const std::string& source, const std::string& error);
    void load_fragments(const std::string& fragments_dir);

    std::map<std::string, BindingEntry> entries_;
    std::string map_error_;
    std::string map_error_source_;
    std::vector<std::string> info_;
    std::vector<std::string> warnings_;
};

// LEMONADE_LLAMACPP_BINDINGS_DIR when set and non-empty, otherwise the
// platform default ("" where there is none).
std::string fragments_dir_from_environment();

// A built-in's cache key is its bare name, not its `builtin.` canonical ID.
std::string normalize_binding_key(const std::string& key);

// Returns "" and fills `out` when the entry is valid, else the reason.
std::string validate_binding_entry(const nlohmann::json& entry, ExecutableBinding& out);

// Independent of install state: a bound model never uses a shared backend
// install, so only the llama.cpp support row for this OS matters.
bool bindable_backend_on_current_os(const std::string& backend);

// "" when the bound executable is a regular file (after symlinks) that this
// process can execute, else the reason.
std::string executable_file_error(const std::string& executable);

// "" unless `requested` names a llama.cpp backend other than the one bound to
// `cache_key`. Values the option system reads as unset never conflict.
std::string backend_conflict_error(const ExecutableBindings& bindings,
                                   const std::string& cache_key,
                                   const nlohmann::json& requested);

// Runs before admission so that a failure can never evict anything.
void check_binding_for_load(const ExecutableBindings& bindings,
                            const std::string& cache_key,
                            const std::string& recipe,
                            const RecipeOptions& request_options,
                            const RecipeOptions& model_options);

void set_executable_bindings(std::shared_ptr<const ExecutableBindings> bindings);
std::shared_ptr<const ExecutableBindings> executable_bindings();

// Loads the table from `config_dir` (the platform default when empty), logs
// it, and installs it. Every entry point that runs lemon::Server must call
// this first, or every binding is silently ignored.
void install_executable_bindings(const std::string& config_dir);

}  // namespace llamacpp
}  // namespace backends
}  // namespace lemon
