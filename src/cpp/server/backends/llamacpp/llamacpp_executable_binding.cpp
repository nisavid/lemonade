#include "lemon/backends/llamacpp/llamacpp_executable_binding.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include <utility>

#include "lemon/backends/llamacpp/llamacpp.h"
#include "lemon/config_file.h"
#include "lemon/recipe_options.h"
#include "lemon/system_info.h"
#include "lemon/utils/aixlog.hpp"
#include "lemon/utils/path_utils.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace lemon {
namespace backends {
namespace llamacpp {

using json = nlohmann::json;

namespace {

constexpr const char* kBuiltinPrefix = "builtin.";
constexpr const char* kFragmentSuffix = ".json";
constexpr const char* kConfigSource = "config.json";

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

const std::vector<std::string>& bindable_backends() {
    static const std::vector<std::string> backends = {
        "cpu", "vulkan", "rocm", "cuda", "metal"};
    return backends;
}

std::string join(const std::vector<std::string>& values) {
    std::string joined;
    for (const auto& value : values) {
        if (!joined.empty()) joined += ", ";
        joined += value;
    }
    return joined;
}

std::string validate_fragment(const json& doc,
                              const std::string& stem,
                              ExecutableBinding& out) {
    const std::string shape =
        "a fragment must hold exactly {\"llamacpp\": {\"model_executables\": "
        "{\"" + stem + "\": {...}}}}";
    if (!doc.is_object() || doc.size() != 1 || !doc.contains("llamacpp")) {
        return shape;
    }
    const json& section = doc.at("llamacpp");
    if (!section.is_object() || section.size() != 1 ||
        !section.contains("model_executables")) {
        return shape;
    }
    const json& executables = section.at("model_executables");
    if (!executables.is_object() || executables.size() != 1) {
        return shape;
    }
    if (!executables.contains(stem)) {
        return "the entry key must equal the file name stem '" + stem + "'";
    }
    const json& entry = executables.at(stem);
    if (entry.is_null()) {
        return "a fragment cannot unbind a model; use null in config.json";
    }
    return validate_binding_entry(entry, out);
}

std::mutex& registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<const ExecutableBindings>& registry_slot() {
    static std::shared_ptr<const ExecutableBindings> slot(
        std::make_shared<ExecutableBindings>());
    return slot;
}

BindingLayer layer_from(const json& root, const std::string& source) {
    BindingLayer layer;
    layer.source = source;
    if (root.is_object() && root.contains("llamacpp") &&
        root.at("llamacpp").is_object() &&
        root.at("llamacpp").contains("model_executables")) {
        layer.present = true;
        layer.value = root.at("llamacpp").at("model_executables");
    }
    return layer;
}

}  // namespace

std::string normalize_binding_key(const std::string& key) {
    return starts_with(key, kBuiltinPrefix)
        ? key.substr(std::string(kBuiltinPrefix).size())
        : key;
}

namespace {

// A source prefix with an empty bare name, such as "user.", is no model's
// cache key, so a binding under it could never apply.
bool names_no_model(const std::string& key) {
    const std::string model = normalize_binding_key(key);
    return model.empty() || model == "user." || model == "extra.";
}

}  // namespace

std::string validate_binding_entry(const json& entry, ExecutableBinding& out) {
    if (!entry.is_object()) {
        return "an entry must be an object with exactly 'executable' and 'backend'";
    }
    for (auto it = entry.begin(); it != entry.end(); ++it) {
        if (it.key() != "executable" && it.key() != "backend") {
            return "unknown field '" + it.key() + "'";
        }
    }
    if (!entry.contains("executable")) return "missing field 'executable'";
    if (!entry.contains("backend")) return "missing field 'backend'";

    const json& executable = entry.at("executable");
    if (!executable.is_string() || executable.get<std::string>().empty()) {
        return "'executable' must be a non-empty string";
    }
    const std::string executable_text = executable.get<std::string>();
    fs::path executable_path;
    try {
        executable_path = utils::path_from_utf8(executable_text);
    } catch (const std::exception&) {
        return "'executable' is not a valid path: " + executable_text;
    }
    if (!executable_path.is_absolute()) {
        return "'executable' must be an absolute path: " + executable_text;
    }
    if (!executable_path.has_filename()) {
        return "'executable' must name a file, not a directory: " + executable_text;
    }
    if (executable_path.lexically_normal() != executable_path) {
        return "'executable' must be lexically normal, without '.' or '..': " +
               executable_text;
    }

    const json& backend = entry.at("backend");
    if (!backend.is_string()) {
        return "'backend' must be a string";
    }
    const std::string backend_text = backend.get<std::string>();
    const auto& allowed = bindable_backends();
    if (std::find(allowed.begin(), allowed.end(), backend_text) == allowed.end()) {
        return "'backend' must be one of: " + join(allowed) + " (got '" +
               backend_text + "')";
    }
    if (!bindable_backend_on_current_os(backend_text)) {
        return "'backend' '" + backend_text +
               "' is not a llama.cpp backend on " + get_current_os();
    }

    out.executable = executable_text;
    out.backend = backend_text;
    return "";
}

std::string fragments_dir_from_environment() {
    const std::string configured =
        utils::get_environment_variable_utf8("LEMONADE_LLAMACPP_BINDINGS_DIR");
    if (!configured.empty()) {
        return configured;
    }
#ifndef _WIN32
    return "/usr/share/lemonade/llamacpp-bindings.d";
#else
    return "";
#endif
}

void ExecutableBindings::add_map_error(const std::string& source,
                                       const std::string& error) {
    if (map_error_.empty()) {
        map_error_ = error;
        map_error_source_ = source;
    } else {
        map_error_ += "; " + error;
        if (source != map_error_source_) {
            map_error_ += " (" + source + ")";
        }
    }
}

void ExecutableBindings::load_fragments(const std::string& fragments_dir) {
    if (fragments_dir.empty()) {
        return;
    }
    const fs::path dir = utils::path_from_utf8(fragments_dir);
    std::error_code ec;
    const bool dir_exists = fs::exists(dir, ec);
    if (ec) {
        add_map_error(fragments_dir,
                      "cannot inspect the binding fragment directory: " + ec.message());
        return;
    }
    if (!dir_exists) {
        return;
    }
    if (!fs::is_directory(dir, ec)) {
        add_map_error(fragments_dir, "the binding fragment path is not a directory");
        return;
    }
    info_.push_back("Reading llama.cpp binding fragments from " + fragments_dir);

    std::vector<std::pair<std::string, fs::path>> files;
    fs::directory_iterator it(dir, ec);
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string name = utils::path_to_utf8(it->path().filename());
        if (name.empty() || name[0] == '.' || !ends_with(name, kFragmentSuffix)) {
            continue;
        }
        files.emplace_back(name, it->path());
    }
    if (ec) {
        add_map_error(fragments_dir,
                      "cannot read the binding fragment directory: " + ec.message());
        return;
    }
    // std::string ordering compares as unsigned char, so this is bytewise.
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::map<std::string, std::vector<std::string>> sources_by_model;
    for (const auto& [name, path] : files) {
        const std::string stem =
            name.substr(0, name.size() - std::string(kFragmentSuffix).size());
        const std::string source = utils::path_to_utf8(path);
        if (names_no_model(stem)) {
            add_map_error(source, "a binding fragment file name names no model");
            continue;
        }
        const std::string model = normalize_binding_key(stem);
        std::string error;
        if (model != stem) {
            error = "the file name must use the bare model name, without 'builtin.'";
        }

        ExecutableBinding binding;
        if (error.empty()) {
            std::error_code file_ec;
            if (!fs::is_regular_file(path, file_ec)) {
                error = "not a regular file";
            } else {
                std::ifstream file(path, std::ios::binary);
                if (!file) {
                    error = "cannot be read";
                } else {
                    try {
                        const json doc = json::parse(file);
                        error = validate_fragment(doc, stem, binding);
                    } catch (const json::exception& e) {
                        error = std::string("invalid JSON: ") + e.what();
                    }
                }
            }
        }

        auto& sources = sources_by_model[model];
        sources.push_back(source);

        BindingEntry entry;
        entry.source = source;
        if (sources.size() > 1) {
            entry.state = BindingState::Rejected;
            entry.error = "more than one fragment names this model: " + join(sources);
        } else if (!error.empty()) {
            entry.state = BindingState::Rejected;
            entry.error = error;
        } else {
            entry.state = BindingState::Bound;
            entry.binding = binding;
            entry.binding.source = source;
        }
        if (entry.state == BindingState::Rejected) {
            warnings_.push_back("Rejected llama.cpp binding fragment " + source +
                                " for model '" + model + "': " + entry.error);
        }
        entries_[model] = entry;
    }
}

ExecutableBindings ExecutableBindings::resolve(
    const std::vector<BindingLayer>& defaults_layers,
    const std::string& fragments_dir,
    const BindingLayer& config_layer) {
    ExecutableBindings bindings;
    bindings.load_fragments(fragments_dir);

    const std::string reserved =
        "'llamacpp.model_executables' is reserved and must not appear in a "
        "defaults file";
    for (const auto& defaults_layer : defaults_layers) {
        if (!defaults_layer.present) {
            continue;
        }
        if (!defaults_layer.value.is_object()) {
            bindings.add_map_error(defaults_layer.source, reserved);
            continue;
        }
        for (auto it = defaults_layer.value.begin(); it != defaults_layer.value.end();
             ++it) {
            if (names_no_model(it.key())) {
                bindings.add_map_error(defaults_layer.source, reserved);
                continue;
            }
            const std::string model = normalize_binding_key(it.key());
            BindingEntry entry;
            entry.state = BindingState::Rejected;
            entry.source = defaults_layer.source;
            entry.error = reserved;
            bindings.warnings_.push_back(
                "Rejected llama.cpp binding for model '" + model + "' from " +
                defaults_layer.source + ": " + reserved);
            bindings.entries_[model] = entry;
        }
    }

    if (config_layer.present) {
        const json& config_map = config_layer.value;
        if (!config_map.is_object()) {
            bindings.add_map_error(
                config_layer.source,
                "'llamacpp.model_executables' must be an object of per-model "
                "entries; unbind a model with a per-model null");
        } else {
            std::map<std::string, std::vector<std::string>> keys_by_model;
            for (auto it = config_map.begin(); it != config_map.end(); ++it) {
                if (names_no_model(it.key())) {
                    bindings.add_map_error(
                        config_layer.source,
                        "'llamacpp.model_executables' key '" + it.key() +
                            "' names no model");
                    continue;
                }
                keys_by_model[normalize_binding_key(it.key())].push_back(it.key());
            }

            for (const auto& [model, keys] : keys_by_model) {
                BindingEntry entry;
                entry.source = config_layer.source;
                if (keys.size() > 1) {
                    entry.state = BindingState::Rejected;
                    entry.error = "keys " + join(keys) + " name the same model";
                } else {
                    const json& value = config_map.at(keys.front());
                    if (value.is_null()) {
                        entry.state = BindingState::Unbound;
                    } else {
                        ExecutableBinding binding;
                        const std::string error = validate_binding_entry(value, binding);
                        if (error.empty()) {
                            entry.state = BindingState::Bound;
                            entry.binding = binding;
                            entry.binding.source = config_layer.source;
                        } else {
                            entry.state = BindingState::Rejected;
                            entry.error = error;
                        }
                    }
                }

                auto previous = bindings.entries_.find(model);
                if (previous != bindings.entries_.end()) {
                    bindings.warnings_.push_back(
                        "config.json " +
                        std::string(entry.state == BindingState::Unbound
                                        ? "unbinds"
                                        : "overrides") +
                        " the llama.cpp binding for model '" + model + "' from " +
                        previous->second.source);
                }
                if (entry.state == BindingState::Rejected) {
                    bindings.warnings_.push_back(
                        "Rejected llama.cpp binding for model '" + model +
                        "' in config.json: " + entry.error);
                }
                bindings.entries_[model] = entry;
            }
        }
    }

    for (const auto& [model, entry] : bindings.entries_) {
        if (entry.state == BindingState::Unbound) {
            bindings.info_.push_back("llama.cpp model '" + model +
                                     "' is unbound by " + entry.source);
            continue;
        }
        if (entry.state != BindingState::Bound) {
            continue;
        }
        bindings.info_.push_back("llama.cpp model '" + model + "' is bound to " +
                                 entry.binding.executable + " (backend " +
                                 entry.binding.backend + ", from " +
                                 entry.binding.source + ")");
        std::error_code ec;
        if (!fs::exists(utils::path_from_utf8(entry.binding.executable), ec)) {
            bindings.warnings_.push_back(
                "The llama.cpp executable bound to model '" + model +
                "' does not exist yet: " + entry.binding.executable);
        }
    }
    if (!bindings.map_error_.empty()) {
        bindings.warnings_.push_back(
            "Every llama.cpp load fails until this binding error is fixed (" +
            bindings.map_error_source_ + "): " + bindings.map_error_);
    }
    return bindings;
}

ExecutableBindings ExecutableBindings::load(const std::string& config_dir) {
    std::vector<BindingLayer> defaults_layers;
    try {
        for (const auto& layer : ConfigFile::get_defaults_layers()) {
            defaults_layers.push_back(layer_from(layer.value, layer.source));
        }
    } catch (const std::exception& e) {
        LOG(WARNING, "LlamaCpp") << "Could not re-read the defaults chain for "
                                 << "executable bindings: " << e.what() << std::endl;
    }
    const BindingLayer config_layer =
        layer_from(ConfigFile::load_raw(config_dir), kConfigSource);
    return resolve(defaults_layers, fragments_dir_from_environment(), config_layer);
}

const BindingEntry* ExecutableBindings::find(const std::string& cache_key) const {
    auto it = entries_.find(cache_key);
    return it != entries_.end() ? &it->second : nullptr;
}

std::optional<ExecutableBinding> ExecutableBindings::bound(
    const std::string& cache_key) const {
    const BindingEntry* entry = find(cache_key);
    if (!entry || entry->state != BindingState::Bound) {
        return std::nullopt;
    }
    return entry->binding;
}

json ExecutableBindings::to_json() const {
    if (!map_error_.empty()) {
        return {{"error", map_error_}, {"source", map_error_source_}};
    }
    json view = json::object();
    for (const auto& [model, entry] : entries_) {
        switch (entry.state) {
            case BindingState::Bound:
                view[model] = {{"executable", entry.binding.executable},
                               {"backend", entry.binding.backend},
                               {"source", entry.binding.source}};
                break;
            case BindingState::Unbound:
                view[model] = nullptr;
                break;
            case BindingState::Rejected:
                view[model] = {{"error", entry.error}, {"source", entry.source}};
                break;
        }
    }
    return view;
}

void ExecutableBindings::log_summary() const {
    for (const auto& line : info_) {
        LOG(INFO, "LlamaCpp") << line << std::endl;
    }
    for (const auto& line : warnings_) {
        LOG(WARNING, "LlamaCpp") << line << std::endl;
    }
}

bool bindable_backend_on_current_os(const std::string& backend) {
    const auto& allowed = bindable_backends();
    if (std::find(allowed.begin(), allowed.end(), backend) == allowed.end()) {
        return false;
    }
    const std::string os = get_current_os();
    return std::any_of(
        descriptor.support.begin(), descriptor.support.end(),
        [&](const BackendSupport& row) {
            return row.backend == backend && row.supported_os.count(os) > 0;
        });
}

std::string executable_file_error(const std::string& executable) {
    const fs::path path = utils::path_from_utf8(executable);
    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    if (status.type() == fs::file_type::not_found) {
        return "the bound executable is missing: " + executable;
    }
    if (ec) {
        return "cannot inspect the bound executable " + executable + ": " + ec.message();
    }
    if (!fs::is_regular_file(status)) {
        return "the bound executable is not a regular file: " + executable;
    }
#ifndef _WIN32
    if (::access(path.c_str(), X_OK) != 0) {
        return "the bound executable is not executable by lemond: " + executable;
    }
#endif
    return "";
}

std::string backend_conflict_error(const ExecutableBindings& bindings,
                                   const std::string& cache_key,
                                   const json& requested) {
    const auto binding = bindings.bound(cache_key);
    if (!binding || RecipeOptions::is_default_sentinel("llamacpp_backend", requested)) {
        return "";
    }
    if (requested.is_string() && requested.get<std::string>() == binding->backend) {
        return "";
    }
    return "Model '" + cache_key + "' is bound to the llama.cpp '" + binding->backend +
           "' backend by " + binding->source + "; llamacpp_backend " +
           requested.dump() + " conflicts with that binding. Omit llamacpp_backend "
           "or set it to \"" + binding->backend + "\".";
}

void check_binding_for_load(const ExecutableBindings& bindings,
                            const std::string& cache_key,
                            const std::string& recipe,
                            const RecipeOptions& request_options,
                            const RecipeOptions& model_options) {
    const bool llamacpp_load = recipe == descriptor.recipe;
    if (llamacpp_load && !bindings.map_error().empty()) {
        throw ExecutableBindingError(
            "llama.cpp executable bindings are misconfigured (" +
            bindings.map_error_source() + "): " + bindings.map_error() +
            ". Fix the configuration and restart lemond.");
    }

    const BindingEntry* entry = bindings.find(cache_key);
    if (!entry || entry->state == BindingState::Unbound) {
        return;
    }
    if (entry->state == BindingState::Rejected) {
        throw ExecutableBindingError(
            "The llama.cpp executable binding for '" + cache_key + "' in " +
            entry->source + " is invalid: " + entry->error +
            ". Fix it and restart lemond.");
    }
    if (!llamacpp_load) {
        throw ExecutableBindingError(
            "'" + cache_key + "' has a llama.cpp executable binding in " +
            entry->binding.source + " but uses recipe '" + recipe + "'");
    }

    const std::string option = "llamacpp_backend";
    if (request_options.has_explicit_option(option)) {
        const std::string conflict = backend_conflict_error(
            bindings, cache_key, request_options.get_explicit_option(option));
        if (!conflict.empty()) {
            throw ExecutableBindingError(conflict);
        }
    }
    if (model_options.has_option(option)) {
        const json saved = model_options.get_option(option);
        if (!backend_conflict_error(bindings, cache_key, saved).empty()) {
            LOG(WARNING, "LlamaCpp")
                << "Ignoring the saved or registered llamacpp_backend " << saved.dump()
                << " for '" << cache_key << "': it is bound to the '"
                << entry->binding.backend << "' backend by " << entry->binding.source
                << std::endl;
        }
    }
}

void set_executable_bindings(std::shared_ptr<const ExecutableBindings> bindings) {
    if (!bindings) {
        bindings = std::make_shared<ExecutableBindings>();
    }
    std::lock_guard<std::mutex> lock(registry_mutex());
    registry_slot() = std::move(bindings);
}

std::shared_ptr<const ExecutableBindings> executable_bindings() {
    std::lock_guard<std::mutex> lock(registry_mutex());
    return registry_slot();
}

void install_executable_bindings(const std::string& config_dir) {
    const std::string effective_config_dir =
        config_dir.empty() ? utils::get_config_dir() : config_dir;
    auto bindings = std::make_shared<ExecutableBindings>(
        ExecutableBindings::load(effective_config_dir));
    bindings->log_summary();
    set_executable_bindings(std::move(bindings));
}

}  // namespace llamacpp
}  // namespace backends
}  // namespace lemon
