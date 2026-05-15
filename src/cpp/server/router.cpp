#include "lemon/router.h"
#include "lemon/backends/llamacpp_server.h"
#include "lemon/backends/fastflowlm_server.h"
#include "lemon/backends/ryzenaiserver.h"
#include "lemon/backends/whisper_server.h"
#include "lemon/backends/kokoro_server.h"
#include "lemon/backends/sd_server.h"
#include "lemon/backends/vllm_server.h"
#include "lemon/server_capabilities.h"
#include "lemon/error_types.h"
#include "lemon/gguf_metadata.h"
#include "lemon/recipe_options.h"
#include "lemon/system_info.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <utility>
#include <lemon/utils/aixlog.hpp>

namespace fs = std::filesystem;

namespace lemon {

Router::Router(RuntimeConfig* config,
               ModelManager* model_manager,
               BackendManager* backend_manager,
               std::function<double()> gpu_memory_sampler)
    : config_(config),
      model_manager_(model_manager),
      backend_manager_(backend_manager),
      gpu_memory_sampler_(std::move(gpu_memory_sampler)) {

    int max = config_->max_loaded_models();
    if (max == -1) {
    LOG(DEBUG, "Router") << "Max loaded models per type: unlimited" << std::endl;
    } else {
    LOG(DEBUG, "Router") << "Max loaded models per type: " << max << std::endl;
    }
}

Router::~Router() {
    LOG(DEBUG, "Router") << "Destructor: unloading all models" << std::endl;
    unload_model("");  // Unload all
}

WrappedServer* Router::find_server_by_model_name(const std::string& model_name) const {
    for (const auto& server : loaded_servers_) {
        if (server->get_model_name() == model_name) {
            return server.get();
        }
    }
    return nullptr;
}

std::string Router::resolve_model_name(const std::string& model_name) const {
    return model_name.empty() ? model_name : model_manager_->resolve_model_name(model_name);
}

WrappedServer* Router::get_most_recent_server() const {
    if (loaded_servers_.empty()) {
        return nullptr;
    }

    WrappedServer* most_recent = loaded_servers_[0].get();
    for (const auto& server : loaded_servers_) {
        if (server->get_last_access_time() > most_recent->get_last_access_time()) {
            most_recent = server.get();
        }
    }
    return most_recent;
}

int Router::count_servers_by_type(ModelType type) const {
    int count = 0;
    for (const auto& server : loaded_servers_) {
        if (server->get_model_type() == type && !server->is_pinned()) {
            count++;
        }
    }
    return count;
}

WrappedServer* Router::find_lru_server_by_type(ModelType type) const {
    WrappedServer* lru = nullptr;

    for (const auto& server : loaded_servers_) {
        if (server->get_model_type() == type && !server->is_pinned()) {
            if (!lru || server->get_last_access_time() < lru->get_last_access_time()) {
                lru = server.get();
            }
        }
    }

    return lru;
}

bool Router::is_config_pinned(const std::string& canonical_model_name) const {
    auto pinned_models = config_->pinned_models();
    return std::find(pinned_models.begin(), pinned_models.end(), canonical_model_name)
        != pinned_models.end();
}

bool Router::has_npu_server() const {
    for (const auto& server : loaded_servers_) {
        if (server->get_device_type() & DEVICE_NPU) {
            return true;
        }
    }
    return false;
}

WrappedServer* Router::find_npu_server() const {
    for (const auto& server : loaded_servers_) {
        if (server->get_device_type() & DEVICE_NPU) {
            return server.get();
        }
    }
    return nullptr;
}

// Helper: Find NPU server with a specific recipe
WrappedServer* Router::find_npu_server_by_recipe(const std::string& recipe) const {
    for (const auto& server : loaded_servers_) {
        if ((server->get_device_type() & DEVICE_NPU) &&
            server->get_recipe_options().get_recipe() == recipe) {
            return server.get();
        }
    }
    return nullptr;
}

// Helper: Find FLM server of a specific model type
WrappedServer* Router::find_flm_server_by_type(ModelType type) const {
    for (const auto& server : loaded_servers_) {
        if (server->get_recipe_options().get_recipe() == "flm" &&
            server->get_model_type() == type) {
            return server.get();
        }
    }
    return nullptr;
}

// Helper: Evict all NPU servers
void Router::evict_all_npu_servers(bool include_pinned) {
    std::vector<WrappedServer*> npu_servers;
    for (const auto& server : loaded_servers_) {
        if (server->get_device_type() & DEVICE_NPU) {
            if (server->is_pinned() && !include_pinned) {
                throw std::runtime_error(
                    "Pinned model requires NPU access and cannot be evicted: "
                    + server->get_model_name());
            }
            npu_servers.push_back(server.get());
        }
    }
    for (auto* server : npu_servers) {
        LOG(INFO, "Router") << "Evicting NPU server: " << server->get_model_name() << std::endl;
        evict_server(server);
    }
}

// Helper: Evict a specific server
void Router::evict_server(WrappedServer* server) {
    if (!server) return;

    std::string model_name = server->get_model_name();
    LOG(INFO, "Router") << "Evicting model: " << model_name << std::endl;

    // Wait for any ongoing inference to complete
    server->wait_until_not_busy();

    // Unload the server
    server->unload();

    // Remove from vector
    loaded_servers_.erase(
        std::remove_if(loaded_servers_.begin(), loaded_servers_.end(),
                      [server](const std::unique_ptr<WrappedServer>& s) {
                          return s.get() == server;
                      }),
        loaded_servers_.end()
    );

    LOG(INFO, "Router") << "Evicted model: " << model_name << std::endl;
}

void Router::evict_all_servers(bool include_pinned) {
    LOG(INFO, "Router") << "Evicting all models (" << loaded_servers_.size() << " total)" << std::endl;

    // Wait for all servers to finish (with timeout to prevent infinite hang).
    for (const auto& server : loaded_servers_) {
        if (server->is_pinned() && !include_pinned) continue;
        server->wait_until_not_busy(EVICTION_TIMEOUT);
    }

    // Unload all
    for (const auto& server : loaded_servers_) {
        if (server->is_pinned() && !include_pinned) continue;
        LOG(INFO, "Router") << "Unloading: " << server->get_model_name() << std::endl;
        server->unload();
    }

    loaded_servers_.erase(
        std::remove_if(loaded_servers_.begin(), loaded_servers_.end(),
                      [include_pinned](const std::unique_ptr<WrappedServer>& server) {
                          return include_pinned || !server->is_pinned();
                      }),
        loaded_servers_.end()
    );
    LOG(INFO, "Router") << "All models evicted" << std::endl;
}

std::unique_ptr<WrappedServer> Router::create_backend_server(const ModelInfo& model_info) {
    std::unique_ptr<WrappedServer> new_server;
    std::string log_level = config_->log_level();

    if (model_info.recipe == "whispercpp") {
    LOG(DEBUG, "Router") << "Creating WhisperServer backend" << std::endl;
        new_server = std::make_unique<backends::WhisperServer>(log_level, model_manager_, backend_manager_);
    } else if (model_info.recipe == "kokoro") {
    LOG(DEBUG, "Router") << "Creating Kokoro backend" << std::endl;
        new_server = std::make_unique<backends::KokoroServer>(log_level, model_manager_, backend_manager_);
    } else if (model_info.recipe == "sd-cpp") {
    LOG(DEBUG, "Router") << "Creating SDServer backend" << std::endl;
        new_server = std::make_unique<backends::SDServer>(log_level, model_manager_, backend_manager_);
    } else if (model_info.recipe == "flm") {
    LOG(DEBUG, "Router") << "Creating FastFlowLM backend" << std::endl;
        new_server = std::make_unique<backends::FastFlowLMServer>(log_level, model_manager_, backend_manager_);
    } else if (model_info.recipe == "ryzenai-llm") {
    LOG(DEBUG, "Router") << "Creating RyzenAI-Server backend" << std::endl;

        std::string model_path = model_info.resolved_path();
    LOG(DEBUG, "Router") << "Using model path: " << model_path << std::endl;

        auto* ryzenai_server = new RyzenAIServer(model_info.model_name,
                                                  log_level == "debug", model_manager_, backend_manager_);
        ryzenai_server->set_model_path(model_path);
        new_server.reset(ryzenai_server);
    } else if (model_info.recipe == "vllm") {
        LOG(DEBUG, "Router") << "Creating vLLM backend" << std::endl;
        new_server = std::make_unique<backends::VLLMServer>(log_level, model_manager_, backend_manager_);
    } else {
    LOG(DEBUG, "Router") << "Creating LlamaCpp backend" << std::endl;
        new_server = std::make_unique<backends::LlamaCppServer>(log_level, model_manager_, backend_manager_);
    }

    return new_server;
}

static bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool Router::should_enforce_gpu_memory_capacity(const ModelInfo& model_info,
                                                const RecipeOptions& options) const {
    if (model_info.recipe == "llamacpp") {
        std::string backend = options.get_option("llamacpp_backend");
        return backend != "cpu";
    }
    if (model_info.recipe == "sd-cpp") {
        std::string backend = options.get_option("sd-cpp_backend");
        return backend.empty() || backend == "vulkan" || starts_with(backend, "rocm");
    }
    if (model_info.recipe == "whispercpp") {
        std::string backend = options.get_option("whispercpp_backend");
        return backend.empty() || backend == "vulkan";
    }
    return (model_info.device & DEVICE_GPU) != 0;
}

bool Router::is_gpu_resident_server(const WrappedServer& server) const {
    ModelInfo server_model_info;
    RecipeOptions options = server.get_recipe_options();
    server_model_info.recipe = options.get_recipe();
    server_model_info.device = server.get_device_type();
    return (server.get_device_type() & DEVICE_GPU) &&
           should_enforce_gpu_memory_capacity(server_model_info, options);
}

double Router::sample_total_gpu_occupancy_gb() const {
    if (!gpu_memory_sampler_) return -1.0;
    try {
        return gpu_memory_sampler_();
    } catch (const std::exception& e) {
        LOG(WARNING, "Router") << "Failed to sample GPU memory occupancy: " << e.what() << std::endl;
        return -1.0;
    }
}

double Router::get_lemonade_gpu_occupancy_gb() const {
    double total = 0.0;
    for (const auto& server : loaded_servers_) {
        if (!server->is_pinned() && is_gpu_resident_server(*server)) {
            total += std::max(0.0, server->get_gpu_memory_occupancy_gb());
        }
    }
    return total;
}

static double gpu_capacity_from_device_json(const json& device, bool include_virtual_memory) {
    double vram_gb = device.value("vram_gb", 0.0);
    double virtual_mem_gb = device.value("virtual_mem_gb", 0.0);
    if (include_virtual_memory) {
        return vram_gb + virtual_mem_gb;
    }
    return vram_gb > 0.0 ? vram_gb : virtual_mem_gb;
}

double Router::get_total_gpu_capacity_gb() const {
    json system_info = SystemInfoCache::get_system_info_with_cache();
    if (!system_info.contains("devices") || !system_info["devices"].is_object()) {
        return 0.0;
    }

    const json& devices = system_info["devices"];
    double single_device_capacity_gb = 0.0;

    auto accumulate_gpu_capacity = [&](const std::string& key, bool include_virtual_memory) {
        if (!devices.contains(key)) return;
        json dev_list = devices[key].is_array() ? devices[key] : json::array({devices[key]});
        for (const auto& device : dev_list) {
            if (!device.is_object() || !device.value("available", false)) continue;
            single_device_capacity_gb = std::max(single_device_capacity_gb,
                                                 gpu_capacity_from_device_json(device, include_virtual_memory));
        }
    };

    // Lemonade does not track per-request GPU placement, so do not treat memory
    // as pooled across cards. The sampler provides aggregate pressure; this
    // capacity is the largest single observable AMD device.
    accumulate_gpu_capacity("amd_gpu", config_->enable_dgpu_gtt());

    return single_device_capacity_gb;
}

double Router::estimate_gpu_memory_occupancy_gb(const ModelInfo& model_info,
                                                const RecipeOptions& options) const {
    const double model_size_gb = std::max(0.0, model_info.size);
    if (model_size_gb <= 0.0) {
        return 1.0;
    }

    if (model_info.recipe == "llamacpp") {
        const std::string path = model_info.resolved_path();
        std::error_code ec;
        if (!path.empty() && fs::exists(fs::u8path(path), ec)) {
            auto metadata = read_gguf_metadata(path);
            if (metadata && metadata->block_count > 0) {
                int64_t ctx_size = options.get_option("ctx_size").get<int64_t>();
                if (ctx_size <= 0) {
                    ctx_size = model_info.max_context_window > 0 ? model_info.max_context_window : 4096;
                }
                if (model_info.type == ModelType::EMBEDDING && ctx_size < 8192) {
                    ctx_size = 8192;
                }
                const int64_t block_count = std::max<int64_t>(0, metadata->block_count);
                const int64_t embedding_length = std::max<int64_t>(0, metadata->embedding_length);
                const int64_t head_count = metadata->head_count > 0 ? metadata->head_count : 1;
                const int64_t kv_heads = metadata->head_count_kv > 0 ? metadata->head_count_kv : head_count;
                const int64_t key_length = metadata->key_length > 0
                    ? metadata->key_length
                    : (embedding_length > 0 ? embedding_length / head_count : 128);
                const int64_t value_length = metadata->value_length > 0 ? metadata->value_length : key_length;
                const double kv_bytes =
                    static_cast<double>(ctx_size) *
                    static_cast<double>(block_count) *
                    static_cast<double>(kv_heads) *
                    static_cast<double>(key_length + value_length) *
                    2.0;
                const double kv_gb = kv_bytes / (1024.0 * 1024.0 * 1024.0);
                return model_size_gb * 1.05 + kv_gb + 1.0;
            }
        }
    }

    return model_size_gb * 1.20 + 1.0;
}

void Router::enforce_gpu_memory_capacity(const ModelInfo& model_info,
                                         const RecipeOptions& options,
                                         const WrappedServer* replacement_server) {
    if (!should_enforce_gpu_memory_capacity(model_info, options)) return;

    const double total_capacity_gb = get_total_gpu_capacity_gb();
    if (total_capacity_gb <= 0.0) {
        LOG(DEBUG, "Router") << "Skipping GPU memory capacity check: total GPU capacity unavailable" << std::endl;
        return;
    }

    const double replacement_occupancy_gb =
        replacement_server && is_gpu_resident_server(*replacement_server)
            ? std::max(0.0, replacement_server->get_gpu_memory_occupancy_gb())
            : 0.0;
    const double total_gpu_occupancy_gb = sample_total_gpu_occupancy_gb();
    const double current_lemonade_occupancy_gb = get_lemonade_gpu_occupancy_gb();
    const double lemonade_occupancy_gb =
        std::max(0.0, current_lemonade_occupancy_gb - replacement_occupancy_gb);
    double free_capacity_gb = total_gpu_occupancy_gb >= 0.0
        ? std::max(0.0, total_capacity_gb - total_gpu_occupancy_gb)
        : std::max(0.0, total_capacity_gb - current_lemonade_occupancy_gb);
    free_capacity_gb += replacement_occupancy_gb;

    GpuMemoryAdmissionInputs inputs;
    inputs.configured_capacity_gb = config_->max_gpu_memory_occupancy_gb();
    inputs.total_capacity_gb = total_capacity_gb;
    inputs.free_capacity_gb = free_capacity_gb;
    inputs.lemonade_occupancy_gb = lemonade_occupancy_gb;
    inputs.candidate_occupancy_gb = estimate_gpu_memory_occupancy_gb(model_info, options);
    for (const auto& server : loaded_servers_) {
        if (server.get() == replacement_server) continue;
        if (!server->is_pinned() && is_gpu_resident_server(*server)) {
            inputs.residents.push_back({
                server->get_model_name(),
                server->get_gpu_memory_occupancy_gb()
            });
        }
    }

    GpuMemoryAdmissionPlan plan = plan_gpu_memory_admission(inputs);
    if (!plan.can_fit) {
        throw std::runtime_error(plan.rejection_reason);
    }

    for (const auto& model_to_evict : plan.models_to_evict) {
        WrappedServer* server = find_server_by_model_name(model_to_evict);
        if (server) {
            LOG(INFO, "Router") << "GPU memory budget requires evicting "
                                << model_to_evict
                                << " (" << server->get_gpu_memory_occupancy_gb() << " GB)"
                                << std::endl;
            evict_server(server);
        }
    }

    if (!plan.models_to_evict.empty()) {
        LOG(INFO, "Router") << "GPU memory budget projected occupancy after eviction: "
                            << plan.projected_occupancy_gb << " GB / "
                            << plan.effective_capacity_gb << " GB" << std::endl;
    }
}

void Router::load_model(const std::string& model_name,
                       const ModelInfo& model_info,
                       RecipeOptions options,
                       bool do_not_upgrade,
                       bool allow_reload_on_option_change,
                       bool pin_model) {
    const std::string canonical_model_name = resolve_model_name(model_name);
    RecipeOptions default_opt = RecipeOptions(model_info.recipe, config_->recipe_options());
    const bool model_should_be_pinned = pin_model || is_config_pinned(canonical_model_name);

    // Resolve settings: load overrides take precedence over per-model overrides which take precedence over defaults
        RecipeOptions effective_options = options.inherit(model_info.recipe_options.inherit(default_opt));

    LOG(DEBUG, "Router") << "Effective settings: " << effective_options.to_log_string() << std::endl;


    // LOAD SERIALIZATION STRATEGY (from spec: point #2 in Additional Considerations)
    std::unique_lock<std::mutex> lock(load_mutex_);

    // Wait if another thread is currently loading
    while (is_loading_) {
    LOG(INFO, "Router") << "Another load is in progress, waiting..." << std::endl;
        load_cv_.wait(lock);
    }

    // Mark that we're now loading (prevents concurrent loads)
    is_loading_ = true;

    LOG(DEBUG, "Router") << "Loading model: " << canonical_model_name
            << " (checkpoint: " << model_info.checkpoint()
            << ", recipe: " << model_info.recipe
            << ", type: " << model_type_to_string(model_info.type)
            << ", device: " << device_type_to_string(model_info.device) << ")" << std::endl;

    try {
        WrappedServer* reload_existing = nullptr;

        // Check if model is already loaded
        WrappedServer* existing = find_server_by_model_name(canonical_model_name);
        if (existing) {
            if (allow_reload_on_option_change &&
                existing->get_recipe_options().to_json() != effective_options.to_json()) {
                LOG(INFO, "Router") << "Options changed, reloading model: " << canonical_model_name << std::endl;
                reload_existing = existing;
                // Fall through to admission checks before unloading the existing instance.
            } else {
                LOG(INFO, "Router") << "Model already loaded, updating access time" << std::endl;
                if (model_should_be_pinned) {
                    existing->set_pinned(true);
                }
                existing->update_access_time();
                is_loading_ = false;
                load_cv_.notify_all();
                return;
            }
        }

        // Determine model type and device
        ModelType model_type = model_info.type;
        DeviceType device_type = model_info.device;

        // Get max models for this type (same limit for all types)
        int max_models = config_->max_loaded_models();

        // NPU EXCLUSIVITY CHECK (recipe-aware rules)
        // FLM can run up to 3 concurrent NPU processes (1 LLM + 1 audio + 1 embedding)
        // RyzenAI and WhisperCpp lock the entire NPU exclusively
        if (device_type & DEVICE_NPU) {
            if (model_info.recipe == "ryzenai-llm" || model_info.recipe == "whispercpp") {
                // Exclusive NPU recipes - evict ALL NPU servers
                if (has_npu_server()) {
                    LOG(INFO, "Router") << model_info.recipe
                              << " requires exclusive NPU access, evicting all NPU servers..." << std::endl;
                    evict_all_npu_servers(/*include_pinned=*/model_should_be_pinned);
                }
            } else if (model_info.recipe == "flm") {
                // FLM can coexist with other FLM types, but not with exclusive-NPU recipes
                // 1. Evict any exclusive-NPU server (mutually exclusive)
                for (const std::string& exclusive_recipe : {"ryzenai-llm", "whispercpp"}) {
                    WrappedServer* exclusive_server = find_npu_server_by_recipe(exclusive_recipe);
                    if (exclusive_server) {
                        if (exclusive_server->is_pinned() && !model_should_be_pinned) {
                            throw std::runtime_error(
                                "Pinned model requires NPU access and cannot be evicted: "
                                + exclusive_server->get_model_name());
                        }
                        LOG(INFO, "Router") << "FLM cannot coexist with " << exclusive_recipe
                                  << ", evicting: " << exclusive_server->get_model_name() << std::endl;
                        evict_server(exclusive_server);
                    }
                }
                // 2. Evict FLM of the SAME model type (max 1 per type: 1 LLM, 1 audio, 1 embed)
                WrappedServer* same_type_flm = find_flm_server_by_type(model_type);
                if (same_type_flm) {
                    if (same_type_flm->is_pinned() && !model_should_be_pinned) {
                        throw std::runtime_error(
                            "Pinned model requires NPU access and cannot be evicted: "
                            + same_type_flm->get_model_name());
                    }
                    LOG(INFO, "Router") << "FLM " << model_type_to_string(model_type)
                              << " slot occupied by: " << same_type_flm->get_model_name()
                              << ", evicting..." << std::endl;
                    evict_server(same_type_flm);
                }
            } else {
                // Unknown NPU recipe - default to exclusive access
                if (has_npu_server()) {
                    LOG(INFO, "Router") << "Unknown NPU recipe, evicting all NPU servers..." << std::endl;
                    evict_all_npu_servers(/*include_pinned=*/model_should_be_pinned);
                }
            }
        }

        const bool requested_gpu = should_enforce_gpu_memory_capacity(model_info, effective_options);
        enforce_gpu_memory_capacity(model_info, effective_options, reload_existing);

        if (reload_existing) {
            WrappedServer* server_to_reload = find_server_by_model_name(canonical_model_name);
            if (server_to_reload) {
                evict_server(server_to_reload);
            }
        }

        // LRU EVICTION CHECK (from spec: Least Recently Used Cache)
        // Skip eviction if unlimited (-1)
        int current_count = count_servers_by_type(model_type);
        if (max_models != -1 && current_count >= max_models) {
            WrappedServer* lru = find_lru_server_by_type(model_type);
            if (lru) {
            LOG(INFO, "Router") << "Slot limit reached for type "
                          << model_type_to_string(model_type)
                          << ", evicting LRU: " << lru->get_model_name() << std::endl;
                evict_server(lru);
            }
        }

        // Create new backend server
        std::unique_ptr<WrappedServer> new_server = create_backend_server(model_info);

        // Set model metadata
        new_server->set_model_metadata(canonical_model_name, model_info.checkpoint(), model_type, device_type, effective_options);
        new_server->set_pinned(model_should_be_pinned);
        new_server->update_access_time();

        // CRITICAL: Release lock before slow backend startup
        const double predicted_gpu_occupancy_gb = requested_gpu
            ? estimate_gpu_memory_occupancy_gb(model_info, effective_options)
            : 0.0;
        const double gpu_occupancy_before_load = requested_gpu ? sample_total_gpu_occupancy_gb() : -1.0;
        lock.unlock();

        // Load the backend (this can take 30-60 seconds)
    LOG(DEBUG, "Router") << "Starting backend (this may take a moment)..." << std::endl;
        bool load_success = false;
        std::string error_message;

        try {
            new_server->load(canonical_model_name, model_info, effective_options, do_not_upgrade);
            load_success = true;
        LOG(DEBUG, "Router") << "Backend started successfully" << std::endl;
        } catch (const std::exception& e) {
            error_message = e.what();
            load_success = false;
        LOG(ERROR, "Router") << "Backend load failed: " << error_message << std::endl;
        }

        lock.lock();

        if (load_success) {
            // Success: Refresh access time so this model is returned by
            // get_most_recent_server() (the pre-load timestamp from line 316
            // may have been overtaken by other models serving requests while
            // the lock was released during the slow backend load).
            new_server->update_access_time();

            // Add to loaded servers
            if (is_gpu_resident_server(*new_server)) {
                double gpu_occupancy_gb = predicted_gpu_occupancy_gb;
                const double gpu_occupancy_after_load = sample_total_gpu_occupancy_gb();
                if (gpu_occupancy_before_load >= 0.0 &&
                    gpu_occupancy_after_load >= gpu_occupancy_before_load) {
                    double measured_delta = gpu_occupancy_after_load - gpu_occupancy_before_load;
                    if (measured_delta > 0.0) {
                        gpu_occupancy_gb = measured_delta;
                    }
                }
                new_server->set_gpu_memory_occupancy_gb(gpu_occupancy_gb);
            }
            loaded_servers_.push_back(std::move(new_server));

            is_loading_ = false;
            load_cv_.notify_all();

        LOG(INFO, "Router") << "Model loaded successfully. Total loaded: "
                      << loaded_servers_.size() << std::endl;
        } else {
            // ERROR HANDLING (from spec: Error Handling section)
            // Check if error is "file not found" (exception to nuclear policy)
            bool is_file_not_found = (error_message.find("not found") != std::string::npos ||
                                     error_message.find("does not exist") != std::string::npos ||
                                     error_message.find("No such file") != std::string::npos);

            is_loading_ = false;
            load_cv_.notify_all();

            if (is_file_not_found) {
            LOG(ERROR, "Router") << "File not found error, NOT evicting other models" << std::endl;
                throw std::runtime_error(error_message);
            }

            // Nuclear option: evict all models and retry
        LOG(WARNING, "Router") << "Load failed with non-file-not-found error, "
                      << "evicting all models and retrying..." << std::endl;

            evict_all_servers();

            // Mark loading again for retry
            is_loading_ = true;

            // Create new server for retry
            std::unique_ptr<WrappedServer> retry_server = create_backend_server(model_info);
            retry_server->set_model_metadata(canonical_model_name, model_info.checkpoint(), model_type, device_type, effective_options);
            retry_server->set_pinned(model_should_be_pinned);
            retry_server->update_access_time();
            const double retry_gpu_occupancy_before_load = requested_gpu ? sample_total_gpu_occupancy_gb() : -1.0;

            lock.unlock();

        LOG(DEBUG, "Router") << "Retrying backend load..." << std::endl;
            try {
                retry_server->load(canonical_model_name, model_info, effective_options, do_not_upgrade);

                lock.lock();

                if (is_gpu_resident_server(*retry_server)) {
                    double retry_gpu_occupancy_gb = predicted_gpu_occupancy_gb;
                    const double retry_gpu_occupancy_after_load = sample_total_gpu_occupancy_gb();
                    if (retry_gpu_occupancy_before_load >= 0.0 &&
                        retry_gpu_occupancy_after_load >= retry_gpu_occupancy_before_load) {
                        double measured_delta = retry_gpu_occupancy_after_load - retry_gpu_occupancy_before_load;
                        if (measured_delta > 0.0) {
                            retry_gpu_occupancy_gb = measured_delta;
                        }
                    }
                    retry_server->set_gpu_memory_occupancy_gb(retry_gpu_occupancy_gb);
                }
                loaded_servers_.push_back(std::move(retry_server));
                is_loading_ = false;
                load_cv_.notify_all();

            LOG(DEBUG, "Router") << "Retry successful!" << std::endl;
            } catch (const std::exception& retry_error) {
                lock.lock();
                is_loading_ = false;
                load_cv_.notify_all();

            LOG(ERROR, "Router") << "Retry also failed: " << retry_error.what() << std::endl;
                throw;
            }
        }

    } catch (const std::exception& e) {
    LOG(ERROR, "Router") << "Failed to load model: " << e.what() << std::endl;

        if (!lock.owns_lock()) {
            lock.lock();
        }
        is_loading_ = false;
        load_cv_.notify_all();

        throw;
    }
}

void Router::unload_model(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(load_mutex_);

    if (model_name.empty()) {
        // Unload all models
    LOG(INFO, "Router") << "Unload all models called" << std::endl;
        evict_all_servers(/*include_pinned=*/true);
    } else {
        // Unload specific model
    LOG(INFO, "Router") << "Unload model called: " << model_name << std::endl;
        std::string canonical_model_name = resolve_model_name(model_name);
        WrappedServer* server = find_server_by_model_name(canonical_model_name);
        if (!server) {
            throw std::runtime_error("Model not loaded: " + model_name);
        }
        evict_server(server);
    }
}

std::string Router::get_loaded_model() const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = get_most_recent_server();
    return server ? model_manager_->get_public_model_name(server->get_model_name()) : "";
}

std::string Router::get_loaded_recipe() const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = get_most_recent_server();
    if (!server) return "";

    // Get the actual recipe from the server's recipe options
    return server->get_recipe_options().get_recipe();
}

json Router::get_all_loaded_models() const {
    std::lock_guard<std::mutex> lock(load_mutex_);

    json result = json::array();

    for (const auto& server : loaded_servers_) {
        json model_info;
        model_info["model_name"] = model_manager_->get_public_model_name(server->get_model_name());
        model_info["checkpoint"] = server->get_checkpoint();
        model_info["type"] = model_type_to_string(server->get_model_type());
        model_info["device"] = device_type_to_string(server->get_device_type());
        model_info["backend_url"] = server->get_address();  // For debugging port issues
        model_info["pid"] = server->get_process_id();
        model_info["gpu_memory_occupancy_gb"] = server->get_gpu_memory_occupancy_gb();
        model_info["pinned"] = server->is_pinned();
        RecipeOptions recipe_options =  server->get_recipe_options();
        model_info["recipe"] = recipe_options.get_recipe();
        model_info["recipe_options"] = recipe_options.to_json();

        // Convert timestamp to milliseconds since epoch
        auto time_point = server->get_last_access_time();
        auto duration = time_point.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        model_info["last_use"] = millis;

        result.push_back(model_info);
    }

    return result;
}

json Router::get_max_model_limits() const {
    int max = config_->max_loaded_models();
    return {
        {"llm", max},
        {"embedding", max},
        {"reranking", max},
        {"audio", max},
        {"image", max},
        {"tts", max}
    };
}

bool Router::is_model_loaded() const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    return !loaded_servers_.empty();
}

bool Router::is_model_loaded(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    return find_server_by_model_name(resolve_model_name(model_name)) != nullptr;
}

RecipeOptions Router::get_model_recipe_options(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    auto* server = find_server_by_model_name(resolve_model_name(model_name));
    if (server) return server->get_recipe_options();
    return RecipeOptions();
}

void Router::set_model_pinned(const std::string& model_name, bool pinned) {
    std::lock_guard<std::mutex> lock(load_mutex_);
    auto* server = find_server_by_model_name(resolve_model_name(model_name));
    if (server) {
        server->set_pinned(pinned);
    }
}

ModelType Router::get_model_type(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = model_name.empty()
        ? get_most_recent_server()
        : find_server_by_model_name(resolve_model_name(model_name));
    return server ? server->get_model_type() : ModelType::LLM;
}

std::string Router::get_backend_address() const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = get_most_recent_server();
    return server ? server->get_address() : "";
}

// Template method for generic inference execution
template<typename Func>
auto Router::execute_inference(const json& request, Func&& inference_func) -> decltype(inference_func(nullptr)) {
    WrappedServer* server = nullptr;

    {
        std::lock_guard<std::mutex> lock(load_mutex_);

        // Extract model from request - required field, no fallback to avoid silent misrouting
        std::string requested_model;
        if (request.contains("model") && request["model"].is_string()) {
            requested_model = request["model"].get<std::string>();
        }

        if (requested_model.empty()) {
            return ErrorResponse::from_exception(InvalidRequestException("No model specified in request"));
        }

        server = find_server_by_model_name(resolve_model_name(requested_model));
        if (!server) {
            return ErrorResponse::from_exception(ModelNotLoadedException(requested_model));
        }

        // Mark as busy and update access time
        server->set_busy(true);
        server->update_access_time();
    } // Lock released here

    // Execute inference without holding lock (but busy flag prevents eviction)
    try {
        auto response = inference_func(server);
        server->set_busy(false);
        return response;
    } catch (...) {
        server->set_busy(false);
        throw;
    }
}

// Template method for streaming execution
template<typename Func>
void Router::execute_streaming(const std::string& request_body, httplib::DataSink& sink, Func&& streaming_func) {
    WrappedServer* server = nullptr;

    {
        std::lock_guard<std::mutex> lock(load_mutex_);

        // Extract model from request body if present (same logic as execute_inference)
        std::string requested_model;
        try {
            json request = json::parse(request_body);
            if (request.contains("model") && request["model"].is_string()) {
                requested_model = request["model"].get<std::string>();
            }
        } catch (...) {
            // If JSON parsing fails, fall back to most recent server
        LOG(DEBUG, "Router") << "Failed to parse request body for model extraction" << std::endl;
        }

        // Find requested model - no fallback to avoid silent misrouting
        if (requested_model.empty()) {
        LOG(ERROR, "Router") << "No model specified in streaming request" << std::endl;
            std::string error_msg = "data: {\"error\":{\"message\":\"No model specified in request\",\"type\":\"invalid_request_error\"}}\n\n";
            sink.write(error_msg.c_str(), error_msg.size());
            return;
        }

        server = find_server_by_model_name(resolve_model_name(requested_model));
        if (!server) {
            std::string error_msg = "data: {\"error\":{\"message\":\"Model not loaded: " + requested_model + "\",\"type\":\"model_not_loaded\"}}\n\n";
            sink.write(error_msg.c_str(), error_msg.size());
            return;
        }

        server->set_busy(true);
        server->update_access_time();
    }

    try {
        streaming_func(server);
        server->set_busy(false);
    } catch (...) {
        server->set_busy(false);
        throw;
    }
}

json Router::chat_completion(const json& request) {
    return execute_inference(request, [&](WrappedServer* server) {
        return server->chat_completion(request);
    });
}

json Router::completion(const json& request) {
    return execute_inference(request, [&](WrappedServer* server) {
        return server->completion(request);
    });
}

json Router::embeddings(const json& request) {
    return execute_inference(request, [&](WrappedServer* server) {
        auto embeddings_server = dynamic_cast<IEmbeddingsServer*>(server);
        if (!embeddings_server) {
            return ErrorResponse::from_exception(
                UnsupportedOperationException("Embeddings", device_type_to_string(server->get_device_type()))
            );
        }
        return embeddings_server->embeddings(request);
    });
}

json Router::reranking(const json& request) {
    return execute_inference(request, [&](WrappedServer* server) {
        auto reranking_server = dynamic_cast<IRerankingServer*>(server);
        if (!reranking_server) {
            return ErrorResponse::from_exception(
                UnsupportedOperationException("Reranking", device_type_to_string(server->get_device_type()))
            );
        }
        return reranking_server->reranking(request);
    });
}

json Router::get_slots() {
    WrappedServer* server = nullptr;
    ISlotsServer* slots_server = nullptr;

    {
        std::lock_guard<std::mutex> lock(load_mutex_);
        server = get_most_recent_server();
        if (!server) {
            return ErrorResponse::from_exception(
                ModelNotLoadedException("No models loaded")
            );
        }

        // Check if server supports slots capability
        slots_server = dynamic_cast<ISlotsServer*>(server);
        if (!slots_server) {
            return ErrorResponse::from_exception(
                UnsupportedOperationException("Slots", device_type_to_string(server->get_device_type()))
            );
        }

        // Mark as busy and update access time
        server->set_busy(true);
        server->update_access_time();
    } // Lock released here

    // Execute without holding lock (but busy flag prevents eviction)
    try {
        auto response = slots_server->get_slots();
        server->set_busy(false);
        return response;
    } catch (...) {
        server->set_busy(false);
        throw;
    }
}

json Router::slots_action(int slot_id, const std::string& action, const json& request_body) {
    WrappedServer* server = nullptr;
    ISlotsServer* slots_server = nullptr;

    {
        std::lock_guard<std::mutex> lock(load_mutex_);
        server = get_most_recent_server();
        if (!server) {
            return ErrorResponse::from_exception(
                ModelNotLoadedException("No models loaded")
            );
        }

        // Check if server supports slots capability
        slots_server = dynamic_cast<ISlotsServer*>(server);
        if (!slots_server) {
            return ErrorResponse::from_exception(
                UnsupportedOperationException("Slots", device_type_to_string(server->get_device_type()))
            );
        }

        // Mark as busy and update access time
        server->set_busy(true);
        server->update_access_time();
    } // Lock released here

    // Execute without holding lock (but busy flag prevents eviction)
    try {
        auto response = slots_server->slots_action(slot_id, action, request_body);
        server->set_busy(false);
        return response;
    } catch (...) {
        server->set_busy(false);
        throw;
    }
}

json Router::responses(const json& request) {
    return execute_inference(request, [&](WrappedServer* server) {
        return server->responses(request);
    });
}

json Router::audio_transcriptions(const json& request) {
    return execute_inference(request, [&](WrappedServer* server) {
        auto audio_server = dynamic_cast<IAudioServer*>(server);
        if (!audio_server) {
            return ErrorResponse::from_exception(
                UnsupportedOperationException("Audio transcription", device_type_to_string(server->get_device_type()))
            );
        }
        return audio_server->audio_transcriptions(request);
    });
}

void Router::audio_speech(const json& request, httplib::DataSink& sink) {
    execute_streaming(request.dump(), sink, [&](WrappedServer* server) {
        auto tts_server = dynamic_cast<ITextToSpeechServer*>(server);
        if (!tts_server) {
            throw UnsupportedOperationException("Text to speech", device_type_to_string(server->get_device_type()));
        }
        tts_server->audio_speech(request, sink);
    });
}

json Router::image_generations(const json& request) {
    return execute_inference(request, [&](WrappedServer* server) {
        auto image_server = dynamic_cast<IImageServer*>(server);
        if (!image_server) {
            return ErrorResponse::from_exception(
                UnsupportedOperationException("Image generation", device_type_to_string(server->get_device_type()))
            );
        }
        return image_server->image_generations(request);
    });
}

json Router::image_edits(const json& request) {
    return execute_inference(request, [&](WrappedServer* server) {
        auto image_server = dynamic_cast<IImageServer*>(server);
        if (!image_server) {
            return ErrorResponse::from_exception(
                UnsupportedOperationException("Image editing", device_type_to_string(server->get_device_type()))
            );
        }
        return image_server->image_edits(request);
    });
}

json Router::image_variations(const json& request) {
    return execute_inference(request, [&](WrappedServer* server) {
        auto image_server = dynamic_cast<IImageServer*>(server);
        if (!image_server) {
            return ErrorResponse::from_exception(
                UnsupportedOperationException("Image variations", device_type_to_string(server->get_device_type()))
            );
        }
        return image_server->image_variations(request);
    });
}

json Router::get_stats() const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = get_most_recent_server();
    if (!server) {
        return ErrorResponse::from_exception(ModelNotLoadedException());
    }
    return server->get_telemetry().to_json();
}

void Router::update_telemetry(int input_tokens, int output_tokens,
                              double time_to_first_token, double tokens_per_second) {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = get_most_recent_server();
    if (server) {
        server->set_telemetry(input_tokens, output_tokens,
                             time_to_first_token, tokens_per_second);
    }
}

void Router::update_prompt_tokens(int prompt_tokens) {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = get_most_recent_server();
    if (server) {
        server->set_prompt_tokens(prompt_tokens);
    }
}

void Router::chat_completion_stream(const std::string& request_body, httplib::DataSink& sink) {
    execute_streaming(request_body, sink, [&](WrappedServer* server) {
        server->forward_streaming_request("/v1/chat/completions", request_body, sink);
    });
}

void Router::completion_stream(const std::string& request_body, httplib::DataSink& sink) {
    execute_streaming(request_body, sink, [&](WrappedServer* server) {
        server->forward_streaming_request("/v1/completions", request_body, sink);
    });
}

void Router::responses_stream(const std::string& request_body, httplib::DataSink& sink) {
    execute_streaming(request_body, sink, [&](WrappedServer* server) {
        server->forward_streaming_request("/v1/responses", request_body, sink);
    });
}

} // namespace lemon
