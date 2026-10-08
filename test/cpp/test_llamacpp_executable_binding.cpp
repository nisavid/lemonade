#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <lemon/backends/llamacpp/llamacpp_executable_binding.h>
#include <lemon/config_file.h>
#include <lemon/recipe_options.h>
#include <lemon/router.h>
#include <lemon/runtime_config.h>
#include <lemon/utils/path_utils.h>
#include <nlohmann/json.hpp>

#include "test_config_helpers.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using lemon::backends::llamacpp::BindingLayer;
using lemon::backends::llamacpp::BindingState;
using lemon::backends::llamacpp::ExecutableBindings;
using test_helpers::check;

namespace lemon {

struct StubBackendState {
    std::atomic<int> loads{0};
    std::atomic<int> unloads{0};
    std::atomic<bool> fail_load{false};
    std::string last_backend;
};

class StubLlamaServer : public WrappedServer {
public:
    explicit StubLlamaServer(std::shared_ptr<StubBackendState> state)
        : WrappedServer("stub", "error", nullptr, nullptr), state_(std::move(state)) {}

    void load(const std::string&, const ModelInfo&, const RecipeOptions& options,
              bool) override {
        ++state_->loads;
        const json backend = options.get_option("llamacpp_backend");
        state_->last_backend = backend.is_string() ? backend.get<std::string>() : "";
        if (state_->fail_load.load()) {
            throw std::runtime_error("llama-server failed to start");
        }
    }

    void unload() override { ++state_->unloads; }

    bool is_backend_alive() const override { return true; }

private:
    std::shared_ptr<StubBackendState> state_;
};

struct LlamaCppExecutableBindingTestHook {
    static void set_backend(Router& router, std::shared_ptr<StubBackendState> state) {
        router.backend_server_factory_ = [state = std::move(state)](const ModelInfo&) {
            return std::make_unique<StubLlamaServer>(state);
        };
        router.available_memory_sampler_ =
            [](DeviceType, GpuMemoryVendor, const std::string&) { return 64.0; };
    }

    static void add_ready_server(Router& router, const std::string& model_name,
                                 std::shared_ptr<StubBackendState> state) {
        auto server = std::make_unique<StubLlamaServer>(std::move(state));
        server->set_model_metadata(model_name, "", ModelType::LLM, DEVICE_CPU,
                                   RecipeOptions("llamacpp", json::object()));
        server->set_state(ModelState::READY);
        std::lock_guard<std::mutex> lock(router.load_mutex_);
        router.loaded_servers_.push_back(std::move(server));
    }
};

}  // namespace lemon

namespace {

unsigned long current_process_id() {
#ifdef _WIN32
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

void set_env_var(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

std::string utf8(const fs::path& path) {
    return lemon::utils::path_to_utf8(path);
}

// A backend that has a llama.cpp support row on every CI host OS.
std::string host_backend() {
#ifdef __APPLE__
    return "metal";
#else
    return "vulkan";
#endif
}

json entry(const std::string& executable, const std::string& backend) {
    return json::object({{"executable", executable}, {"backend", backend}});
}

json fragment(const std::string& key, const json& value) {
    json doc;
    doc["llamacpp"]["model_executables"][key] = value;
    return doc;
}

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

void write_json(const fs::path& path, const json& doc) {
    write_text(path, doc.dump(2));
}

BindingLayer layer(const json& value, const std::string& source) {
    BindingLayer result;
    result.present = true;
    result.value = value;
    result.source = source;
    return result;
}

bool has_state(const ExecutableBindings& bindings,
               const std::string& model,
               BindingState state) {
    const auto* found = bindings.find(model);
    return found && found->state == state;
}


lemon::ModelInfo model_info(const std::string& name, const std::string& recipe = "llamacpp") {
    lemon::ModelInfo info;
    info.model_name = name;
    info.recipe = recipe;
    info.type = lemon::ModelType::LLM;
    info.device = lemon::DEVICE_CPU;
    info.recipe_options = lemon::RecipeOptions(recipe, json::object());
    return info;
}

// Returns the load error, or "" when the load succeeded.
std::string try_load(lemon::Router& router, const std::string& name,
                 const json& request = json::object(),
                 const std::string& recipe = "llamacpp") {
    try {
        auto preparation = router.prepare_model_load(name, lemon::LoadPurpose::UserInference);
        const auto info = model_info(name, recipe);
        router.load_prepared_model(std::move(preparation), info,
                                   lemon::RecipeOptions(recipe, request));
        return "";
    } catch (const std::exception& e) {
        const std::string message = e.what();
        return message.empty() ? "(empty error)" : message;
    }
}

bool has_text(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::string backend_of(const lemon::RecipeOptions& options) {
    const json value = options.get_option("llamacpp_backend");
    return value.is_string() ? value.get<std::string>() : "";
}

std::string args_of(const lemon::RecipeOptions& options) {
    const json value = options.get_option("llamacpp_args");
    return value.is_string() ? value.get<std::string>() : "";
}

void install_bindings(const json& config_map) {
    lemon::backends::llamacpp::set_executable_bindings(
        std::make_shared<ExecutableBindings>(
            ExecutableBindings::resolve({}, "", layer(config_map, "config.json"))));
}

void run_router_tests(const fs::path& runtime, const std::string& backend) {
    json values = lemon::ConfigFile::get_defaults();
    values["max_loaded_models"] = 1;
    values["log_level"] = "error";
    values["offline"] = true;
    values["no_fetch_executables"] = true;
    values["llamacpp"][backend + "_args"] = "--threads 3";
    lemon::RuntimeConfig config(values);
    lemon::RuntimeConfig::set_global(&config);

    const fs::path good_path = runtime / "bound-llama-server";
    write_text(good_path, "#!/bin/sh\n");
    std::error_code perms_ec;
    fs::permissions(good_path, fs::perms::owner_exec | fs::perms::owner_read,
                    fs::perm_options::add, perms_ec);
    const std::string good = utf8(good_path);
    const std::string conflicting = backend == "cpu" ? "vulkan" : "cpu";

    json bindings = json::object();
    bindings["Bad-Path"] = entry(utf8(runtime / "missing-llama-server"), backend);
    bindings["Good-Bound"] = entry(good, backend);
    bindings["Fail-Bound"] = entry(good, backend);
    bindings["Conflict-Bound"] = entry(good, backend);
    bindings["Unset-Bound"] = entry(good, backend);
    bindings["Foo"] = entry(good, backend);
    bindings["Rejected-Bound"] = entry("relative/llama-server", backend);
    bindings["Stub-Recipe"] = entry(good, backend);
    install_bindings(bindings);

    {
        lemon::Router router(&config, nullptr, nullptr);
        auto state = std::make_shared<lemon::StubBackendState>();
        lemon::LlamaCppExecutableBindingTestHook::set_backend(router, state);
        auto resident = std::make_shared<lemon::StubBackendState>();
        lemon::LlamaCppExecutableBindingTestHook::add_ready_server(
            router, "Resident", resident);

        const std::string bad = try_load(router, "Bad-Path");
        check(has_text(bad, "missing-llama-server"),
              "a missing bound executable fails the load with its path");
        check(state->loads.load() == 0, "a bad bound path never starts the backend");
        check(resident->unloads.load() == 0 && router.is_model_loaded("Resident"),
              "a bad bound path fails before admission evicts the resident");

        config.set({{"max_loaded_models", 4}});

        check(try_load(router, "Good-Bound").empty(), "a bound model loads");
        check(state->last_backend == backend,
              "the bound backend reaches the backend server");

        check(has_text(try_load(router, "Conflict-Bound", {{"llamacpp_backend", conflicting}}),
                       "conflicts"),
              "a request naming a different backend is rejected");
        check(!router.is_model_loaded("Conflict-Bound"), "the rejected load leaves nothing loaded");
        check(try_load(router, "Unset-Bound", {{"llamacpp_backend", "auto"}}).empty(),
              "an unset request backend proceeds");

        check(has_text(try_load(router, "Rejected-Bound"), "invalid"),
              "a rejected binding fails the named model's loads");
        check(has_text(try_load(router, "Stub-Recipe", json::object(), "stub"), "uses recipe"),
              "a binding on a non-llama.cpp model fails its loads");

        const int resident_unloads = resident->unloads.load();
        const int bound_unloads = state->unloads.load();
        state->fail_load = true;
        const std::string failed = try_load(router, "Fail-Bound");
        check(has_text(failed, "llama-server failed to start"),
              "a failed bound start reports the original error");
        check(resident->unloads.load() == resident_unloads &&
                  state->unloads.load() == bound_unloads &&
                  router.is_model_loaded("Resident") && router.is_model_loaded("Good-Bound"),
              "a failed bound start never evicts other models");

        try_load(router, "Fail-Unbound");
        check(resident->unloads.load() > resident_unloads,
              "an unbound failed start still takes the evict-all retry");
        state->fail_load = false;
    }

    {
        lemon::Router router(&config, nullptr, nullptr);
        const lemon::RecipeOptions no_request("llamacpp", json::object());
        const auto with_bindings =
            router.resolve_effective_options(model_info("Plain"), no_request, "Plain");
        const auto foo = router.resolve_effective_options(model_info("Foo"), no_request, "Foo");
        const auto user_foo =
            router.resolve_effective_options(model_info("user.Foo"), no_request, "user.Foo");
        const auto conflicting_request = router.resolve_effective_options(
            model_info("Foo"),
            lemon::RecipeOptions("llamacpp", {{"llamacpp_backend", conflicting}}), "Foo");
        check(backend_of(foo) == backend,
              "the binding forces the effective backend");
        check(has_text(args_of(foo), "--threads"),
              "the bound backend selects its llamacpp.<backend>_args");
        check(!has_text(args_of(user_foo), "--threads"),
              "an unbound model keeps the shared backend's args");
        check(backend_of(conflicting_request) == backend,
              "effective options never throw and keep the bound backend");

        // A listing names a shadowed built-in by its public ID, and the load
        // path names the winner's ModelInfo by the bare name it shares.
        const auto shadowed_builtin =
            router.resolve_effective_options(model_info("builtin.Foo"), no_request, "Foo");
        const auto shadowing_user =
            router.resolve_effective_options(model_info("Foo"), no_request, "user.Foo");
        check(backend_of(shadowed_builtin) == backend &&
                  has_text(args_of(shadowed_builtin), "--threads"),
              "the cache key, not the model name, selects a shadowed built-in's binding");
        check(shadowing_user.to_json() == user_foo.to_json(),
              "a cache key without a binding stays unbound whatever the model name");

        lemon::backends::llamacpp::set_executable_bindings(nullptr);
        const auto without_bindings =
            router.resolve_effective_options(model_info("Plain"), no_request, "Plain");
        const auto user_foo_unbound =
            router.resolve_effective_options(model_info("user.Foo"), no_request, "user.Foo");
        check(with_bindings.to_json() == without_bindings.to_json(),
              "an unbound model resolves exactly as with no bindings");
        check(user_foo.to_json() == user_foo_unbound.to_json(),
              "a user. model that shadows a bound built-in stays unbound");
    }

    const fs::path unnamed_fragments = runtime / "unnamed-bindings.d";
    fs::create_directories(unnamed_fragments);
    write_json(unnamed_fragments / "user..json", fragment("user.", entry(good, backend)));
    json unnamed_key = json::object();
    unnamed_key["user."] = entry(good, backend);
    const std::vector<std::pair<std::string, ExecutableBindings>> misconfigured = {
        {"a malformed config.json map",
         ExecutableBindings::resolve({}, "", layer(nullptr, "config.json"))},
        {"a config.json key that names no model",
         ExecutableBindings::resolve({}, "", layer(unnamed_key, "config.json"))},
        {"a fragment file name that names no model",
         ExecutableBindings::resolve({}, utf8(unnamed_fragments), {})},
        {"a fragment path that is not a directory",
         ExecutableBindings::resolve({}, good, {})},
    };
    for (const auto& [label, resolved] : misconfigured) {
        lemon::backends::llamacpp::set_executable_bindings(
            std::make_shared<ExecutableBindings>(resolved));
        lemon::Router router(&config, nullptr, nullptr);
        auto state = std::make_shared<lemon::StubBackendState>();
        lemon::LlamaCppExecutableBindingTestHook::set_backend(router, state);
        check(has_text(try_load(router, "Any-Llama"), "misconfigured"),
              (label + " fails every llama.cpp load").c_str());
        check(state->loads.load() == 0,
              (label + " never starts a llama.cpp backend").c_str());
        check(try_load(router, "Any-Other", json::object(), "stub").empty(),
              (label + " leaves other recipes loading").c_str());
    }

    lemon::backends::llamacpp::set_executable_bindings(nullptr);
    lemon::RuntimeConfig::set_global(nullptr);
}

}  // namespace

int main() {
    std::puts("=== RUNNING LLAMA.CPP EXECUTABLE BINDING TESTS ===");

    const fs::path root = fs::temp_directory_path() /
        ("lemonade_executable_binding_" + std::to_string(current_process_id()) +
         "_" + std::to_string(std::time(nullptr)));
    const fs::path fragments = root / "llamacpp-bindings.d";
    fs::create_directories(fragments);
    const fs::path runtime = root / "runtime";
    fs::create_directories(runtime);
    const std::string shared_exe = utf8(runtime / "llama-server");
    const std::string other_exe = utf8(runtime / "other-llama-server");
    write_text(runtime / "llama-server", "#!/bin/sh\n");
    write_text(runtime / "other-llama-server", "#!/bin/sh\n");
    const std::string backend = host_backend();

    {
        const auto none = ExecutableBindings::resolve({}, "", {});
        check(none.empty(), "no layers and no fragments yield an empty table");
        check(none.to_json() == json::object(), "an empty table renders as {}");
        check(!none.bound("Anything").has_value(), "unknown models are unbound");
    }

    write_json(fragments / "Bound-Model.json",
               fragment("Bound-Model", entry(shared_exe, backend)));
    write_text(fragments / "Broken-Model.json", "{ not json");
    write_json(fragments / "builtin.Prefixed-Model.json",
               fragment("builtin.Prefixed-Model", entry(shared_exe, backend)));
    write_json(fragments / "Wrong-Key.json",
               fragment("Other-Key", entry(shared_exe, backend)));
    write_json(fragments / "Null-Entry.json", fragment("Null-Entry", nullptr));
    json extra_field = entry(shared_exe, backend);
    extra_field["args"] = "--foo";
    write_json(fragments / "Extra-Field.json", fragment("Extra-Field", extra_field));
    json extra_top = fragment("Extra-Top", entry(shared_exe, backend));
    extra_top["port"] = 1;
    write_json(fragments / "Extra-Top.json", extra_top);
    write_json(fragments / "Dup-Model.json",
               fragment("Dup-Model", entry(shared_exe, backend)));
    write_json(fragments / "builtin.Dup-Model.json",
               fragment("builtin.Dup-Model", entry(shared_exe, backend)));
    write_json(fragments / "user.Registered.json",
               fragment("user.Registered", entry(shared_exe, backend)));
    write_json(fragments / "Overridden.json",
               fragment("Overridden", entry(shared_exe, backend)));
    write_json(fragments / "Unbound-Later.json",
               fragment("Unbound-Later", entry(shared_exe, backend)));
    write_json(fragments / "Rejected-Later.json",
               fragment("Rejected-Later", entry(shared_exe, backend)));
    write_json(fragments / "Fixed-Later.json", fragment("Fixed-Later", nullptr));
    write_json(fragments / ".Hidden.json", fragment(".Hidden", entry(shared_exe, backend)));
    write_text(fragments / "README.txt", "not a fragment");

    {
        const auto bindings = ExecutableBindings::resolve({}, utf8(fragments), {});
        const auto bound = bindings.bound("Bound-Model");
        check(bound.has_value(), "a valid fragment binds its model");
        check(bound && bound->executable == shared_exe, "the bound executable is kept verbatim");
        check(bound && bound->backend == backend, "the bound backend is kept");
        check(bound && bound->source == utf8(fragments / "Bound-Model.json"),
              "the binding records its fragment as the source");
        check(has_state(bindings, "Broken-Model", BindingState::Rejected),
              "an unparseable fragment rejects the model its file name names");
        check(has_state(bindings, "Prefixed-Model", BindingState::Rejected),
              "a builtin.-prefixed file name is rejected and attributed to the bare name");
        check(bindings.find("builtin.Prefixed-Model") == nullptr,
              "the prefixed name never becomes a key");
        check(has_state(bindings, "Wrong-Key", BindingState::Rejected),
              "an entry key that differs from the file stem is rejected");
        check(has_state(bindings, "Null-Entry", BindingState::Rejected),
              "a fragment cannot unbind with null");
        check(has_state(bindings, "Extra-Field", BindingState::Rejected),
              "unknown entry fields are rejected");
        check(has_state(bindings, "Extra-Top", BindingState::Rejected),
              "keys outside llamacpp.model_executables are rejected");
        check(has_state(bindings, "Dup-Model", BindingState::Rejected),
              "two fragments naming one model reject that model");
        check(bindings.bound("user.Registered").has_value(),
              "a user. cache key binds through a fragment of the same name");
        check(bindings.find(".Hidden") == nullptr, "dot files are ignored");
        check(bindings.find("README") == nullptr, "non-.json files are ignored");
        check(bindings.map_error().empty(), "per-model fragment errors are not map errors");

        json view = bindings.to_json();
        check(view["Bound-Model"]["executable"] == json(shared_exe) &&
                  view["Bound-Model"]["backend"] == json(backend) &&
                  view["Bound-Model"].contains("source"),
              "the view shows a bound entry with its source");
        check(view["Broken-Model"].contains("error") && view["Broken-Model"].contains("source"),
              "the view shows a rejected entry with error and source");
    }

    {
        json config = json::object();
        config["Overridden"] = entry(other_exe, backend);
        config["Unbound-Later"] = nullptr;
        config["Rejected-Later"] = entry("relative/llama-server", backend);
        config["Fixed-Later"] = entry(other_exe, backend);
        config["Broken-Model"] = entry(other_exe, backend);
        config["builtin.Config-Only"] = entry(other_exe, backend);
        config["Twice"] = entry(other_exe, backend);
        config["builtin.Twice"] = entry(other_exe, backend);

        const auto bindings = ExecutableBindings::resolve(
            {}, utf8(fragments), layer(config, "config.json"));
        const auto overridden = bindings.bound("Overridden");
        check(overridden && overridden->executable == other_exe &&
                  overridden->source == "config.json",
              "a config.json entry replaces the fragment entry whole");
        check(has_state(bindings, "Unbound-Later", BindingState::Unbound),
              "a config.json null unbinds a fragment binding");
        check(!bindings.bound("Unbound-Later").has_value(),
              "an unbound model has no binding");
        check(has_state(bindings, "Rejected-Later", BindingState::Rejected),
              "a rejected config.json entry never falls back to the fragment");
        check(bindings.bound("Fixed-Later").has_value(),
              "a valid config.json entry outranks a malformed fragment");
        check(bindings.bound("Broken-Model").has_value(),
              "a valid config.json entry outranks an unparseable fragment");
        check(bindings.bound("Config-Only").has_value() &&
                  bindings.find("builtin.Config-Only") == nullptr,
              "a builtin. config.json key is normalized to the bare cache key");
        check(has_state(bindings, "Twice", BindingState::Rejected),
              "two config.json keys for one model reject that model");
        check(bindings.bound("Bound-Model").has_value(),
              "fragment bindings without a config.json entry still apply");
        check(bindings.to_json()["Unbound-Later"].is_null(),
              "the view shows a config.json unbinding as null");
    }

    {
        const auto bindings = ExecutableBindings::resolve(
            {}, utf8(fragments), layer(nullptr, "config.json"));
        check(!bindings.map_error().empty(), "a map-level null is malformed, not unbind-all");
        check(bindings.map_error_source() == "config.json", "the map error names config.json");
        json view = bindings.to_json();
        check(view.is_object() && view["error"].is_string() && view["source"] == json("config.json"),
              "the view reports the map-level error");

        const auto not_object = ExecutableBindings::resolve(
            {}, "", layer(json::array(), "config.json"));
        check(!not_object.map_error().empty(), "a non-object map is malformed");
    }

    {
        json defaults = json::object();
        defaults["Default-Bound"] = entry(shared_exe, backend);
        json config = json::object();
        config["Default-Fixed"] = entry(other_exe, backend);
        defaults["Default-Fixed"] = entry(shared_exe, backend);
        const auto bindings = ExecutableBindings::resolve(
            {layer(defaults, "defaults")}, "", layer(config, "config.json"));
        check(has_state(bindings, "Default-Bound", BindingState::Rejected),
              "the key is reserved in the defaults layers");
        check(bindings.bound("Default-Fixed").has_value(),
              "a config.json entry outranks a reserved-key violation for its model");
        check(bindings.map_error().empty(),
              "per-model reserved-key violations are not map errors");
    }

    {
        json upper = json::object();
        upper["Upper-Model"] = entry(shared_exe, backend);
        const auto bindings = ExecutableBindings::resolve(
            {layer(json::array(), "lower-defaults.json"), layer(upper, "upper-defaults.json")},
            "", {});
        check(!bindings.map_error().empty(),
              "a non-object section in a lower defaults layer fails every llama.cpp load "
              "even when a higher layer has an object section");
        check(bindings.map_error_source() == "lower-defaults.json",
              "the map error names the defaults layer that holds the non-object section");
        check(has_state(bindings, "Upper-Model", BindingState::Rejected),
              "the higher layer's entries are still rejected");
    }

    {
        const fs::path env_defaults = root / "env-defaults.json";
        json doc = json::object();
        doc["llamacpp"]["model_executables"] = json::array();
        write_json(env_defaults, doc);
        set_env_var("LEMONADE_DEFAULTS_PATH", utf8(env_defaults).c_str());
        set_env_var("LEMONADE_LLAMACPP_BINDINGS_DIR", utf8(root / "absent.d").c_str());

        const auto layers = lemon::ConfigFile::get_defaults_layers();
        check(!layers.empty() && layers.back().source == utf8(env_defaults),
              "LEMONADE_DEFAULTS_PATH is the highest defaults layer, named by its path");

        const fs::path config_dir = root / "defaults-layer-config";
        fs::create_directories(config_dir);
        const auto bindings = ExecutableBindings::load(utf8(config_dir));
        check(bindings.map_error_source() == utf8(env_defaults) ||
                  bindings.map_error().find(utf8(env_defaults)) != std::string::npos,
              "loading reports a non-object defaults section with its file path");

        set_env_var("LEMONADE_LLAMACPP_BINDINGS_DIR", nullptr);
        set_env_var("LEMONADE_DEFAULTS_PATH", nullptr);
    }

    {
        auto rejects = [&](const json& value, const char* label) {
            json config = json::object();
            config["Candidate"] = value;
            const auto bindings = ExecutableBindings::resolve(
                {}, "", layer(config, "config.json"));
            check(has_state(bindings, "Candidate", BindingState::Rejected), label);
        };
        rejects(entry("relative/llama-server", backend), "a relative executable is rejected");
        rejects(entry(utf8(runtime / "sub" / ".." / "llama-server"), backend),
                "a non-normal executable path is rejected");
        rejects(entry(utf8(runtime / ""), backend), "a directory path is rejected");
        rejects(entry("", backend), "an empty executable is rejected");
        rejects(json::object({{"executable", 7}, {"backend", backend}}), "a non-string executable is rejected");
        rejects(entry(shared_exe, "auto"), "backend auto is rejected");
        rejects(entry(shared_exe, ""), "an empty backend is rejected");
        rejects(entry(shared_exe, "system"), "backend system is rejected");
        rejects(entry(shared_exe, "rocm-stable"), "a ROCm channel name is rejected");
        rejects(json::object({{"executable", shared_exe}}), "a missing backend is rejected");
        rejects(json::object({{"backend", backend}}), "a missing executable is rejected");
        rejects(json::array(), "a non-object entry is rejected");
#ifdef __APPLE__
        rejects(entry(shared_exe, "vulkan"), "a backend without a support row on this OS is rejected");
#else
        rejects(entry(shared_exe, "metal"), "a backend without a support row on this OS is rejected");
#endif
    }

    {
        json config = json::object();
        config["Missing-Exe"] = entry(utf8(runtime / "absent-llama-server"), backend);
        const auto bindings = ExecutableBindings::resolve({}, "", layer(config, "config.json"));
        check(bindings.bound("Missing-Exe").has_value(),
              "a missing executable only warns at startup");
        bool warned = false;
        for (const auto& line : bindings.warnings()) {
            warned = warned || line.find("absent-llama-server") != std::string::npos;
        }
        check(warned, "the missing executable is named in a startup warning");
    }

    {
        const auto missing_dir = ExecutableBindings::resolve({}, utf8(root / "absent.d"), {});
        check(missing_dir.empty(), "a missing fragment directory binds nothing");
        const auto file_dir = ExecutableBindings::resolve(
            {}, utf8(runtime / "llama-server"), {});
        check(!file_dir.map_error().empty(),
              "a fragment path that is not a directory fails every llama.cpp load");
    }

    for (const std::string stem : {"builtin.", "user.", "extra.", "builtin.user."}) {
        const fs::path unnamed = root / ("unnamed-" + std::to_string(stem.size()) + ".d");
        fs::create_directories(unnamed);
        write_json(unnamed / (stem + ".json"), fragment(stem, entry(shared_exe, backend)));
        const auto bindings = ExecutableBindings::resolve({}, utf8(unnamed), {});
        const std::string desc =
            "a fragment file name that names no model fails every llama.cpp load: " + stem +
            ".json";
        check(!bindings.map_error().empty() && bindings.entries().empty(), desc.c_str());
    }

    for (const std::string key : {"", "builtin.", "user.", "extra."}) {
        json config = json::object();
        config[key] = entry(shared_exe, backend);
        const auto bindings = ExecutableBindings::resolve({}, "", layer(config, "config.json"));
        const std::string desc =
            "a config.json key that names no model fails every llama.cpp load: '" + key + "'";
        check(!bindings.map_error().empty() && bindings.entries().empty(), desc.c_str());
    }

    {
        set_env_var("LEMONADE_LLAMACPP_BINDINGS_DIR", utf8(fragments).c_str());
        check(lemon::backends::llamacpp::fragments_dir_from_environment() == utf8(fragments),
              "LEMONADE_LLAMACPP_BINDINGS_DIR replaces the fragment directory");
        set_env_var("LEMONADE_LLAMACPP_BINDINGS_DIR", nullptr);
#ifndef _WIN32
        check(lemon::backends::llamacpp::fragments_dir_from_environment() ==
                  "/usr/share/lemonade/llamacpp-bindings.d",
              "the default fragment directory is the distro path");
#endif
    }

    {
        json config = json::object();
        config["Global-Model"] = entry(shared_exe, backend);
        lemon::backends::llamacpp::set_executable_bindings(
            std::make_shared<ExecutableBindings>(
                ExecutableBindings::resolve({}, "", layer(config, "config.json"))));
        check(lemon::backends::llamacpp::executable_bindings()->bound("Global-Model").has_value(),
              "the process-wide table returns the installed bindings");
        lemon::backends::llamacpp::set_executable_bindings(nullptr);
        check(lemon::backends::llamacpp::executable_bindings()->empty(),
              "clearing the process-wide table leaves an empty table");
    }

    run_router_tests(runtime, backend);

    std::error_code ec;
    fs::remove_all(root, ec);
    return test_helpers::report_results("llama.cpp executable binding");
}
