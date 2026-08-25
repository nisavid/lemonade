#include "lemon/router.h"
#include "lemon/cloud_provider_registry.h"
#include "lemon/backends/backend_registry.h"
#include "lemon/backends/cloud/cloud_server.h"
#include "lemon/backends/llamacpp/llamacpp.h"
#include "lemon/backends/llamacpp/llamacpp_server.h"
#include "lemon/backends/fastflowlm/fastflowlm_server.h"
#include "lemon/backends/ryzenai/ryzenai_server.h"
#include "lemon/backends/whispercpp/whispercpp_server.h"
#include "lemon/backends/moonshine/moonshine_server.h"
#include "lemon/backends/kokoro/kokoro_server.h"
#include "lemon/backends/sdcpp/sdcpp_server.h"
#include "lemon/backends/vllm/vllm_server.h"
#include "lemon/server_capabilities.h"
#include "lemon/streaming_proxy.h"
#include "lemon/error_types.h"
#include "lemon/recipe_options.h"
#include "lemon/system_info.h"
#include "lemon/auto_tune.h"
#include "telemetry.h"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>
#include "lemon/utils/aixlog.hpp"
#include "lemon/global_vram_monitor.h"
#include "lemon/eviction_engine.h"
#include "lemon/suspend_inhibitor.h"
#include "lemon/utils/http_client.h"

namespace fs = std::filesystem;

namespace lemon {

namespace {

// RAII: holds a suspend-inhibitor refcount for the duration of one inference,
// but only when the feature is enabled in config. Released on scope exit so all
// early-return/exception paths are covered.
class InhibitGuard {
public:
    InhibitGuard(SuspendInhibitor* inhibitor, bool enabled)
        : inhibitor_(enabled ? inhibitor : nullptr) {
        if (inhibitor_) inhibitor_->acquire();
    }
    ~InhibitGuard() {
        if (inhibitor_) inhibitor_->release();
    }
    InhibitGuard(const InhibitGuard&) = delete;
    InhibitGuard& operator=(const InhibitGuard&) = delete;

private:
    SuspendInhibitor* inhibitor_;
};

} // namespace

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

    vram_monitor_ = std::make_unique<GlobalVramMonitor>();
    eviction_engine_ = std::make_unique<EvictionEngine>(this, vram_monitor_.get());
    suspend_inhibitor_ = create_suspend_inhibitor();
    reclaim_executor_ = std::make_shared<RoutingHelperReclaimExecutor>(
        [this](const std::string& model_name) { reclaim_stale_helper_if_idle(model_name); });

    // Always start the monitor/engine threads; they are cheap no-ops until the
    // user opts in. The monitor skips the VRAM poll when auto_evict is disabled,
    // and the engine's per-server check skips models that haven't opted in.
    // (auto_evict can be toggled at runtime via /internal/set, so we cannot gate
    // thread creation on the construction-time config value.)
    vram_monitor_->start();
    eviction_engine_->start();
}

Router::~Router() {
    LOG(DEBUG, "Router") << "Destructor: stopping monitors and unloading all models" << std::endl;
    if (eviction_engine_) eviction_engine_->stop();
    if (vram_monitor_) vram_monitor_->stop();
    // Wake any reclaim task blocked waiting for the residency slot, then join the
    // executor before we tear down the state its tasks touch.
    {
        std::lock_guard<std::mutex> lock(load_mutex_);
        reclaim_shutdown_ = true;
    }
    load_cv_.notify_all();
    if (reclaim_executor_) reclaim_executor_->stop();
    unload_model("");  // Unload all
}

Router::PreparedModelLoad::PreparedModelLoad(
    PreparedModelLoad&& other) noexcept
    : router_(other.router_),
      already_loaded_(other.already_loaded_),
      canonical_model_name_(std::move(other.canonical_model_name_)),
      generation_(other.generation_),
      load_purpose_(other.load_purpose_),
      existing_model_policy_(other.existing_model_policy_),
      watchdog_options_(std::move(other.watchdog_options_)) {
    other.router_ = nullptr;
}

Router::PreparedModelLoad::~PreparedModelLoad() {
    if (router_) {
        router_->release_prepared_model_load(
            canonical_model_name_, generation_);
    }
}

Router::ExclusiveRequest::ExclusiveRequest(
    ExclusiveRequest&& other) noexcept
    : router_(other.router_),
      generation_(other.generation_),
      result_(other.result_),
      deadline_(other.deadline_) {
    other.router_ = nullptr;
}

Router::ExclusiveRequest::~ExclusiveRequest() {
    if (router_) {
        router_->cancel_exclusive_request(generation_);
    }
}

Router::ModelRuntimeMutation::ModelRuntimeMutation(
    ModelRuntimeMutation&& other) noexcept
    : router_(other.router_),
      canonical_model_name_(std::move(other.canonical_model_name_)),
      generation_(other.generation_) {
    other.router_ = nullptr;
}

Router::ModelRuntimeMutation::~ModelRuntimeMutation() {
    if (router_) {
        router_->complete_model_runtime_mutation(
            canonical_model_name_, generation_);
    }
}

void Router::set_cloud_registry(CloudProviderRegistry* registry) {
    cloud_registry_ = registry;
}

WrappedServer* Router::find_server_by_model_name(const std::string& model_name) const {
    WrappedServer* unavailable_match = nullptr;
    for (const auto& server : loaded_servers_) {
        if (server->get_model_name() != model_name) {
            continue;
        }
        if (server->is_backend_alive()) {
            return server.get();
        }
        if (!unavailable_match) {
            unavailable_match = server.get();
        }
    }
    return unavailable_match;
}

std::string Router::resolve_model_name(const std::string& model_name) const {
    return model_name.empty() || !model_manager_
        ? model_name
        : model_manager_->resolve_model_name(model_name);
}

std::optional<ModelInfo> Router::try_get_model_info(const std::string& model_name) const {
    if (model_name.empty()) {
        return std::nullopt;
    }
    try {
        if (!model_manager_->model_exists(model_name)) {
            return std::nullopt;
        }
        return model_manager_->get_model_info(model_name);
    } catch (...) {
        return std::nullopt;
    }
}

WrappedServer* Router::get_most_recent_server() const {
    auto most_recent_in_class = [this](ResidencyClass residency_class) {
        WrappedServer* most_recent = nullptr;
        for (const auto& server : loaded_servers_) {
            if (!server->is_backend_alive() ||
                server->get_residency_class() != residency_class) {
                continue;
            }
            if (!most_recent ||
                server->get_last_access_time() > most_recent->get_last_access_time()) {
                most_recent = server.get();
            }
        }
        return most_recent;
    };

    // Internal routing dependencies must not become the implicit user-facing
    // model merely because the classifier ran immediately before dispatch.
    if (auto* standard = most_recent_in_class(ResidencyClass::Standard)) {
        return standard;
    }
    return most_recent_in_class(ResidencyClass::RoutingHelper);
}

void Router::prune_unavailable_servers_locked() {
    std::vector<WrappedServer*> unavailable;
    for (const auto& server : loaded_servers_) {
        if (!server->is_backend_alive()) {
            const std::string model_name = server->get_model_name();
            const std::string checkpoint = server->get_checkpoint();
            const std::string recipe = server->get_recipe_options().get_recipe();
            RecipeOptions watchdog_options = server->get_recipe_options();
            watchdog_options.remove_option("pinned");
            auto pending = pending_reload_states_.find(model_name);
            if (pending == pending_reload_states_.end() ||
                (pending->second.phase != PendingReloadPhase::Cancelled &&
                 (pending->second.checkpoint != checkpoint ||
                  pending->second.recipe != recipe))) {
                const auto generation = ++next_load_generation_;
                pending_reload_states_[model_name] = {
                    generation,
                    PendingReloadPhase::Pending,
                    checkpoint,
                    recipe,
                    watchdog_options,
                    server->is_pinned(),
                    server->get_residency_class()};
            } else if (pending->second.phase == PendingReloadPhase::Pending) {
                pending->second.watchdog_options = watchdog_options;
                pending->second.pinned = server->is_pinned();
                pending->second.residency_class = server->get_residency_class();
            }
            unavailable.push_back(server.get());
        }
    }

    for (auto* server : unavailable) {
        LOG(WARNING, "Router") << "Pruning unavailable backend for model: "
                                << server->get_model_name()
                                << " (state=" << server->get_backend_health_state() << ")"
                                << std::endl;
        evict_server(server);
    }
}

uint64_t Router::cancel_pending_reload_locked(
    const std::string& canonical_model_name) {
    std::string checkpoint;
    std::string recipe;
    auto pending = pending_reload_states_.find(canonical_model_name);
    if (pending != pending_reload_states_.end()) {
        checkpoint = pending->second.checkpoint;
        recipe = pending->second.recipe;
    } else if (auto* server = find_server_by_model_name(canonical_model_name)) {
        checkpoint = server->get_checkpoint();
        recipe = server->get_recipe_options().get_recipe();
    }

    const auto generation = ++next_load_generation_;
    pending_reload_states_[canonical_model_name] = {
        generation,
        PendingReloadPhase::Cancelled,
        checkpoint,
        recipe,
        std::nullopt,
        std::nullopt,
        ResidencyClass::Standard};
    load_cv_.notify_all();
    return generation;
}

bool Router::reload_generation_matches_locked(
    const std::string& canonical_model_name,
    uint64_t generation,
    const ModelInfo& model_info,
    ExistingModelPolicy existing_model_policy) const {
    auto pending = pending_reload_states_.find(canonical_model_name);
    if (pending == pending_reload_states_.end() ||
        pending->second.generation != generation ||
        pending->second.phase != PendingReloadPhase::Pending) {
        return false;
    }
    if (existing_model_policy != ExistingModelPolicy::ReuseExisting) {
        return true;
    }
    return (pending->second.checkpoint.empty() &&
            pending->second.recipe.empty()) ||
           (pending->second.checkpoint == model_info.checkpoint() &&
            pending->second.recipe == model_info.recipe);
}

void Router::complete_reload_generation_locked(
    const std::string& canonical_model_name,
    std::optional<uint64_t> generation) {
    if (!generation.has_value()) {
        return;
    }
    auto pending = pending_reload_states_.find(canonical_model_name);
    if (pending != pending_reload_states_.end() &&
        pending->second.generation == *generation &&
        pending->second.phase == PendingReloadPhase::Pending) {
        pending_reload_states_.erase(pending);
    }
}

void Router::release_prepared_model_load(
    const std::string& canonical_model_name,
    uint64_t generation) noexcept {
    std::lock_guard<std::mutex> lock(load_mutex_);
    auto count = prepared_load_counts_.find(canonical_model_name);
    if (count != prepared_load_counts_.end()) {
        if (--count->second == 0) {
            prepared_load_counts_.erase(count);
        }
    }
    auto pending = pending_reload_states_.find(canonical_model_name);
    if (pending != pending_reload_states_.end() &&
        pending->second.generation == generation &&
        pending->second.phase == PendingReloadPhase::Pending &&
        pending->second.checkpoint.empty() &&
        pending->second.recipe.empty()) {
        pending_reload_states_.erase(pending);
    }
    load_cv_.notify_all();
}

void Router::cancel_exclusive_request(uint64_t generation) noexcept {
    auto expected = generation;
    if (exclusive_pending_generation_.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        load_cv_.notify_all();
    }
}

void Router::complete_model_runtime_mutation(
    const std::string& canonical_model_name,
    uint64_t generation) noexcept {
    std::lock_guard<std::mutex> lock(load_mutex_);
    auto pending = pending_reload_states_.find(canonical_model_name);
    if (pending != pending_reload_states_.end() &&
        pending->second.generation == generation &&
        pending->second.phase == PendingReloadPhase::Cancelled) {
        pending_reload_states_.erase(pending);
    }
    load_cv_.notify_all();
}

bool Router::is_watchdog_reset_response(const json& response) const {
    if (!response.is_object() || !response.contains("error") || !response["error"].is_object()) {
        return false;
    }

    const auto& error = response["error"];
    if (error.contains("code") && error["code"] == "backend_watchdog_reset") {
        return true;
    }
    if (error.contains("details") && error["details"].is_object() &&
        error["details"].contains("code") &&
        error["details"]["code"] == "backend_watchdog_reset") {
        return true;
    }
    return false;
}

bool Router::reload_model_after_watchdog_reset(const std::string& requested_model, const RecipeOptions& options) {
    try {
        LOG(WARNING, "Router") << "Reloading model after backend watchdog reset: "
                                << requested_model << std::endl;
        auto preparation = prepare_model_load(
            requested_model, LoadPurpose::UserInference,
            ExistingModelPolicy::ReuseExisting,
            RecoveryResidencyPolicy::PreserveWatchdog);
        if (preparation.already_loaded()) {
            return true;
        }

        auto info = model_manager_->get_model_info(requested_model);
        RecipeOptions restart_options = options;
        restart_options.remove_option("pinned");
        load_prepared_model(
            std::move(preparation), info, restart_options, true);
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR, "Router") << "Automatic reload after watchdog reset failed for "
                              << requested_model << ": " << e.what() << std::endl;
        return false;
    }
}

// Slot/eviction policy for a recipe, from its descriptor (default Standard).
// This is the recipe-static policy used for pre-load slot decisions.
static SlotPolicy slot_policy_for_recipe(const std::string& recipe) {
    if (const auto* desc = backends::descriptor_for(recipe)) {
        return desc->slot_policy;
    }
    return SlotPolicy::Standard;
}

static bool is_unmetered_recipe(const std::string& recipe) {
    return slot_policy_for_recipe(recipe) == SlotPolicy::Unmetered;
}

int Router::count_servers_in_pool(ModelType type,
                                  ResidencyClass residency_class,
                                  const std::string& model_name) const {
    int count = 0;
    for (const auto& server : loaded_servers_) {
        // Unmetered backends (cloud) consume no local memory and therefore do
        // not consume either a standard or routing-helper capacity slot.
        if (is_unmetered_recipe(server->get_recipe_options().get_recipe())) {
            continue;
        }
        if (server->is_backend_alive() &&
            same_residency_pool(server->get_model_type(),
                                server->get_residency_class(),
                                server->get_model_name(),
                                type,
                                residency_class,
                                model_name)) {
            count++;
        }
    }
    return count;
}

WrappedServer* Router::find_lru_server_in_pool(
    ModelType type,
    ResidencyClass residency_class,
    const std::string& model_name) const {
    WrappedServer* lru = nullptr;

    for (const auto& server : loaded_servers_) {
        // Unmetered backends (cloud) are not eviction candidates; they have no
        // local memory cost and retain provider/upstream bindings while warm.
        if (is_unmetered_recipe(server->get_recipe_options().get_recipe())) {
            continue;
        }
        if (server->is_backend_alive() &&
            same_residency_pool(server->get_model_type(),
                                server->get_residency_class(),
                                server->get_model_name(),
                                type,
                                residency_class,
                                model_name)) {
            if (!is_automatic_eviction_candidate(
                    server->is_pinned(), server->is_busy())) {
                continue;
            }
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

void Router::ensure_residency_capacity(
    ModelType type,
    ResidencyClass residency_class,
    const std::string& model_name) {
    const int limit = residency_limit(residency_class, config_->max_loaded_models());
    if (limit == -1 || count_servers_in_pool(type, residency_class, model_name) < limit) {
        return;
    }

    WrappedServer* lru = find_lru_server_in_pool(type, residency_class, model_name);
    if (!lru) {
        throw SlotsPinnedException(residency_pool_to_string(type, residency_class));
    }

    LOG(INFO, "Router") << "Slot limit reached for pool "
                         << residency_pool_to_string(type, residency_class)
                         << ", evicting LRU: " << lru->get_model_name() << std::endl;
    evict_server(lru);
}

void Router::transition_server_residency_locked(
    WrappedServer* server,
    ResidencyClass requested_residency_class) {
    if (!server || server->get_residency_class() == requested_residency_class) {
        return;
    }

    if (!is_unmetered_recipe(server->get_recipe_options().get_recipe())) {
        // Admit into the destination pool before changing the live role. Standard
        // admission remains type-wide; helper admission is keyed by model name.
        ensure_residency_capacity(
            server->get_model_type(), requested_residency_class,
            server->get_model_name());
    }

    const ResidencyClass previous = server->get_residency_class();
    server->set_residency_class(requested_residency_class);

    LOG(INFO, "Router") << "Changed loaded model " << server->get_model_name()
                         << " residency from "
                         << residency_class_to_string(previous)
                         << " to "
                         << residency_class_to_string(requested_residency_class)
                         << std::endl;
}

Router::PreparedModelLoad Router::prepare_model_load(
    const std::string& model_name,
    LoadPurpose load_purpose,
    ExistingModelPolicy existing_model_policy,
    RecoveryResidencyPolicy recovery_residency_policy) {
    return prepare_model_load_internal(
        model_name, load_purpose, existing_model_policy,
        recovery_residency_policy);
}

Router::PreparedModelLoad Router::prepare_model_load_internal(
    const std::string& model_name,
    LoadPurpose load_purpose,
    ExistingModelPolicy existing_model_policy,
    RecoveryResidencyPolicy recovery_residency_policy) {
    const ResidencyClass requested_residency_class =
        residency_class_for_load_purpose(load_purpose);

    std::unique_lock<std::mutex> lock(load_mutex_);
    std::string canonical_model_name;
    while (true) {
        canonical_model_name = resolve_model_name(model_name);
        wait_for_load_state(lock, [this, &canonical_model_name] {
            auto pending = pending_reload_states_.find(canonical_model_name);
            const bool mutation_in_progress =
                pending != pending_reload_states_.end() &&
                pending->second.phase == PendingReloadPhase::Cancelled;
            const bool preparation_in_progress =
                prepared_load_counts_.find(canonical_model_name) !=
                prepared_load_counts_.end();
            return !all_model_mutation_active_ && !is_loading_ &&
                   !mutation_in_progress && !preparation_in_progress &&
                   !exclusive_pending() &&
                   (!exclusive_active_ ||
                    exclusive_owner_ == std::this_thread::get_id());
        });
        if (resolve_model_name(model_name) == canonical_model_name) {
            break;
        }
    }

    prune_unavailable_servers_locked();

    WrappedServer* existing =
        find_server_by_model_name(canonical_model_name);
    if (existing && existing->is_backend_alive() &&
        existing_model_policy == ExistingModelPolicy::ReuseExisting) {
        if (requested_residency_class != ResidencyClass::RoutingHelper ||
            existing->get_residency_class() == ResidencyClass::RoutingHelper ||
            is_needed_helper_locked(canonical_model_name)) {
            transition_server_residency_locked(
                existing, requested_residency_class);
        }
        existing->update_access_time();
        return PreparedModelLoad(
            nullptr,
            true,
            canonical_model_name,
            0,
            load_purpose,
            existing_model_policy,
            std::nullopt);
    }

    auto pending = pending_reload_states_.find(canonical_model_name);
    std::string checkpoint;
    std::string recipe;
    std::optional<RecipeOptions> watchdog_options;
    std::optional<bool> retained_pinned;
    if (pending != pending_reload_states_.end()) {
        checkpoint = pending->second.checkpoint;
        recipe = pending->second.recipe;
        watchdog_options = pending->second.watchdog_options;
        retained_pinned = pending->second.pinned;
        if (recovery_residency_policy ==
                RecoveryResidencyPolicy::PreserveWatchdog &&
            (!pending->second.checkpoint.empty() ||
             !pending->second.recipe.empty())) {
            load_purpose = load_purpose_for_residency_class(
                pending->second.residency_class);
        }
    }

    const auto generation = ++next_load_generation_;
    pending_reload_states_[canonical_model_name] = {
        generation,
        PendingReloadPhase::Pending,
        std::move(checkpoint),
        std::move(recipe),
        watchdog_options,
        retained_pinned,
        residency_class_for_load_purpose(load_purpose)};
    ++prepared_load_counts_[canonical_model_name];
    return PreparedModelLoad(
        this,
        false,
        canonical_model_name,
        generation,
        load_purpose,
        existing_model_policy,
        std::move(watchdog_options));
}

void Router::abandon_prepared_model_load(PreparedModelLoad preparation) {
    if (preparation.already_loaded_ || preparation.generation_ == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(load_mutex_);
    auto pending = pending_reload_states_.find(
        preparation.canonical_model_name_);
    if (pending != pending_reload_states_.end() &&
        pending->second.generation == preparation.generation_ &&
        pending->second.phase == PendingReloadPhase::Pending &&
        pending->second.checkpoint.empty() &&
        pending->second.recipe.empty()) {
        pending_reload_states_.erase(pending);
    }
}

void Router::reconcile_routing_helpers(const std::set<std::string>& needed_helper_models,
                                       uint64_t generation) {
    // Canonicalize the policy-authored names outside the lock so they match the
    // (already-canonical) live WrappedServer::get_model_name() during eviction.
    std::set<std::string> needed;
    for (const auto& model : needed_helper_models) {
        needed.insert(resolve_model_name(model));
    }
    apply_routing_helper_reconcile(std::move(needed), generation);
}

void Router::apply_routing_helper_reconcile(std::set<std::string> needed, uint64_t generation) {
    std::unique_lock<std::mutex> lock(load_mutex_);

    // Discard a notification that lost a race to a newer one. Policy callbacks can
    // run concurrently and finish out of order; republishing an older set would
    // resurrect helpers a newer policy already dropped (or drop ones it re-added).
    // The newest generation always carries the final registry state, so keeping
    // only the highest generation converges on the authoritative set.
    if (generation <= last_reconcile_generation_) {
        return;
    }
    last_reconcile_generation_ = generation;

    // Publish the authoritative set immediately, even while a load is in flight.
    // A helper's backend loads with load_mutex_ released, so a concurrent load
    // re-acquires the lock at completion and validates against this fresh set
    // (see load_prepared_model) — closing the load-versus-policy-change race without a
    // timer. Deferring the publish behind the wait below would let that load
    // commit an already-obsolete helper.
    needed_helper_models_ = std::move(needed);

    // Only the eviction pass must wait for a quiet slot: evict_server mutates
    // loaded_servers_ and blocks on request drain, neither of which is safe to
    // interleave with an in-flight load.
    wait_for_load_state(lock, [&] {
        return !is_loading_ &&
               !exclusive_pending() &&
               (!exclusive_active_ ||
                exclusive_owner_ == std::this_thread::get_id());
    });
    prune_stale_routing_helpers_locked();
}

void Router::prune_stale_routing_helpers_locked() {
    // Collect first; evict_server mutates loaded_servers_.
    std::vector<WrappedServer*> stale;
    for (const auto& server : loaded_servers_) {
        if (!server->is_backend_alive() ||
            server->get_residency_class() != ResidencyClass::RoutingHelper) {
            continue;
        }
        if (reclaim_or_defer_helper_locked(server.get())) {
            stale.push_back(server.get());
        }
    }

    for (auto* server : stale) {
        LOG(INFO, "Router") << "Routing helper " << server->get_model_name()
                            << " referenced by no active policy, evicting" << std::endl;
        evict_server(server);
    }
}

bool Router::reclaim_or_defer_helper_locked(WrappedServer* server) {
    // A user pin is an explicit "keep" and a policy re-adding the model both
    // cancel a pending reclaim; only a still-stale helper is a candidate.
    if (!routing_helper_no_longer_needed(server->get_model_name(),
                                         ResidencyClass::RoutingHelper,
                                         server->is_pinned())) {
        server->clear_pending_stale();
        return false;
    }
    // Atomically: if the helper is still busy, arm the release-triggered reclaim
    // (evicting now would block on the request drain); if it went idle in the
    // meantime, report so the caller evicts it directly. Doing both under one
    // state lock closes the check-then-arm lost-wakeup race.
    if (server->mark_pending_stale_if_busy()) {
        return false;
    }
    return true;
}

void Router::reclaim_stale_helper_if_idle(const std::string& model_name) {
    std::unique_lock<std::mutex> lock(load_mutex_);

    while (true) {
        // Wait for the residency slot to clear rather than giving up and relying
        // on a later prune (not guaranteed to run when the load / exclusive
        // session ends). The wait is broken on shutdown so the executor worker
        // can drain and join.
        wait_for_load_state(lock, [&] {
            return reclaim_shutdown_ ||
                   (!is_loading_ &&
                    !exclusive_pending() &&
                    (!exclusive_active_ || exclusive_owner_ == std::this_thread::get_id()));
        });
        if (reclaim_shutdown_) {
            return;
        }

        WrappedServer* server = find_server_by_model_name(model_name);
        if (!server || !server->is_backend_alive() ||
            server->get_residency_class() != ResidencyClass::RoutingHelper) {
            return;
        }

        // Rescued (pin / re-added) or still busy: the shared helper cancels or
        // re-arms the reclaim and reports there is nothing to evict right now.
        if (!reclaim_or_defer_helper_locked(server)) {
            return;
        }

        // Stale and idle at the decision above. Commit the eviction atomically; a
        // request that slipped in between rescues the model and this returns false.
        if (server->try_evict_if_idle()) {
            server->clear_pending_stale();
            LOG(INFO, "Router") << "Routing helper " << model_name
                                << " released and referenced by no active policy, evicting"
                                << std::endl;
            evict_server(server);
            return;
        }

        // A request rescued the model between the idle decision and the commit.
        // This reclaim already consumed the pending intent when it was dispatched,
        // so we must restore it: re-arm if the helper is busy again. If it went
        // idle within this window, mark_pending_stale_if_busy reports so and we
        // retry the commit immediately instead of opening a fresh check/arm gap.
        if (server->mark_pending_stale_if_busy()) {
            return;
        }
    }
}

void Router::install_reclaim_notifier(WrappedServer* server) {
    std::weak_ptr<RoutingHelperReclaimExecutor> weak_executor = reclaim_executor_;
    std::string model_name = server->get_model_name();
    server->set_reclaim_notifier([weak_executor, model_name] {
        if (auto executor = weak_executor.lock()) {
            executor->post(model_name);
        }
    });
}

bool Router::is_needed_helper_locked(const std::string& canonical_model_name) const {
    return needed_helper_models_.count(canonical_model_name) != 0;
}

bool Router::routing_helper_no_longer_needed(const std::string& canonical_model_name,
                                             ResidencyClass requested_residency_class,
                                             bool pinned) const {
    // Only routing helpers are subject to policy churn; a pin is an explicit
    // "keep" that outranks it. Any other backend is never considered stale.
    if (requested_residency_class != ResidencyClass::RoutingHelper || pinned) {
        return false;
    }
    return !is_needed_helper_locked(canonical_model_name);
}

bool Router::has_npu_server() const {
    for (const auto& server : loaded_servers_) {
        if (server->is_backend_alive() && (server->get_device_type() & DEVICE_NPU)) {
            return true;
        }
    }
    return false;
}

WrappedServer* Router::find_npu_server() const {
    for (const auto& server : loaded_servers_) {
        if (server->is_backend_alive() && (server->get_device_type() & DEVICE_NPU)) {
            return server.get();
        }
    }
    return nullptr;
}

WrappedServer* Router::find_npu_server_by_recipe(const std::string& recipe) const {
    for (const auto& server : loaded_servers_) {
        if (server->is_backend_alive() &&
            (server->get_device_type() & DEVICE_NPU) &&
            server->get_recipe_options().get_recipe() == recipe) {
            return server.get();
        }
    }
    return nullptr;
}

WrappedServer* Router::find_coexisting_server_by_type(ModelType type) const {
    for (const auto& server : loaded_servers_) {
        if (server->is_backend_alive() &&
            slot_policy_for_recipe(server->get_recipe_options().get_recipe()) ==
                SlotPolicy::CoexistByType &&
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
        if (server->is_backend_alive() && (server->get_device_type() & DEVICE_NPU)) {
            if (server->is_pinned() && !include_pinned) {
                throw std::runtime_error(
                    "Pinned model requires NPU access and cannot be evicted: "
                    + server->get_model_name());
            }
            if (server->is_busy() && !include_pinned) {
                throw std::runtime_error(
                    "In-use model requires NPU access and cannot be evicted: "
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

void Router::evict_server(WrappedServer* server, int timeout_seconds) {
    if (!server) return;

    std::string model_name = server->get_model_name();
    LOG(INFO, "Router") << "Evicting model: " << model_name << std::endl;

    // Wait for any ongoing inference to complete. For watchdog-reset/dead
    // backends the wait is bounded so recovery can continue, but the object must
    // not be destroyed while a request thread may still be using this raw
    // pointer. In that case we leave a tombstoned server in loaded_servers_;
    // future prune/load calls will remove it once the request unwinds.
    const int wait_timeout = server->is_backend_alive() ? timeout_seconds : EVICTION_TIMEOUT;
    const bool idle = server->wait_until_not_busy(wait_timeout);
    if (!idle) {
        LOG(WARNING, "Router") << "Deferring eviction for model " << model_name
                                << " because requests are still unwinding after "
                                << EVICTION_TIMEOUT << "s (state="
                                << server->get_backend_health_state() << ")"
                                << std::endl;
        return;
    }

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

    // Copy raw pointers first; evict_server may erase entries and move
    // unique_ptrs inside the vector, but the pointed-to WrappedServer objects
    // remain stable until their individual eviction completes. Busy/dead
    // servers are safely left as tombstones for a later prune pass.
    std::vector<WrappedServer*> servers;
    servers.reserve(loaded_servers_.size());

    if (!include_pinned) {
        for (const auto& server : loaded_servers_) {
            if (server->is_pinned()) {
                throw std::runtime_error(
                    "Pinned model cannot be evicted by automatic recovery: " +
                    server->get_model_name());
            }
            if (server->is_busy()) {
                throw std::runtime_error(
                    "In-use model cannot be evicted by automatic recovery: " +
                    server->get_model_name());
            }
        }
    }

    for (const auto& server : loaded_servers_) {
        servers.push_back(server.get());
    }

    for (auto* server : servers) {
        evict_server(server, EVICTION_TIMEOUT);
    }

    LOG(INFO, "Router") << "Evict all completed. Remaining tombstoned models: "
                         << loaded_servers_.size() << std::endl;
}

void Router::simulate_vram_pressure(double pct) {
    if (vram_monitor_) {
        vram_monitor_->simulate_pressure(pct);
    }
}

std::unique_ptr<WrappedServer> Router::create_backend_server(const ModelInfo& model_info) {
    if (backend_server_factory_) {
        return backend_server_factory_(model_info);
    }

    std::string log_level = config_->log_level();

    backends::BackendContext ctx;
    ctx.log_level = log_level;
    ctx.model_manager = model_manager_;
    ctx.backend_manager = backend_manager_;
    ctx.cloud_registry = cloud_registry_;
    ctx.model_info = &model_info;

    // The backend registry binds each recipe to its create() (see LEMON_BACKENDS).
    std::unique_ptr<WrappedServer> new_server = backends::create_server(model_info.recipe, ctx);
    if (new_server) {
        LOG(DEBUG, "Router") << "Created backend for recipe '" << model_info.recipe
                             << "' via registry" << std::endl;
        return new_server;
    }

    // Unknown recipe: fall back to llamacpp, preserving the historical default.
    LOG(DEBUG, "Router") << "No registered backend for recipe '" << model_info.recipe
                         << "', defaulting to LlamaCpp" << std::endl;
    return std::make_unique<backends::LlamaCppServer>(log_level, model_manager_, backend_manager_);
}

Router::ExclusiveRequest Router::request_exclusive(
    std::atomic<bool>* cancel) {
    return request_exclusive_impl(cancel, std::nullopt);
}

Router::ExclusiveRequest Router::request_exclusive_until(
    std::chrono::steady_clock::time_point deadline,
    std::atomic<bool>* cancel) {
    return request_exclusive_impl(cancel, deadline);
}

bool Router::exclusive_pending() const noexcept {
    return exclusive_pending_generation_.load(std::memory_order_acquire) != 0;
}

Router::ExclusiveRequest Router::request_exclusive_impl(
    std::atomic<bool>* cancel,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
    std::unique_lock<std::mutex> lock(load_mutex_, std::defer_lock);
    if (deadline.has_value()) {
        if (std::chrono::steady_clock::now() >= *deadline)
            return ExclusiveRequest(
                nullptr, 0, ExclusiveAcquireResult::DeadlineExceeded);
        if (!lock.try_lock())
            return ExclusiveRequest(
                nullptr, 0, ExclusiveAcquireResult::Retry);
        if (std::chrono::steady_clock::now() >= *deadline)
            return ExclusiveRequest(
                nullptr, 0, ExclusiveAcquireResult::DeadlineExceeded);
    } else {
        lock.lock();
    }
    const auto caller = std::this_thread::get_id();
    if (cancel && cancel->load()) {
        return ExclusiveRequest(
            nullptr, 0, ExclusiveAcquireResult::Cancelled);
    }
    if (exclusive_active_ && exclusive_owner_ == caller) {
        return ExclusiveRequest(
            nullptr, 0, ExclusiveAcquireResult::InvalidOrder);
    }
    if (exclusive_active_ || exclusive_pending()) {
        return ExclusiveRequest(
            nullptr, 0, ExclusiveAcquireResult::Retry);
    }
    if (++next_exclusive_generation_ == 0)
        ++next_exclusive_generation_;
    exclusive_admission_generation_ = next_load_generation_;
    exclusive_pending_generation_.store(next_exclusive_generation_,
                                        std::memory_order_release);
    load_cv_.notify_all();
    return ExclusiveRequest(
        this, next_exclusive_generation_, ExclusiveAcquireResult::Retry,
        deadline);
}

Router::ExclusiveAcquireResult Router::try_begin_exclusive(
    ExclusiveRequest& request,
    std::atomic<bool>* cancel) {
    return try_begin_exclusive_impl(request, cancel);
}

Router::ExclusiveAcquireResult Router::try_begin_exclusive_impl(
    ExclusiveRequest& request,
    std::atomic<bool>* cancel) {
    if (request.router_ != this) {
        return request.result_;
    }
    auto deadline_exceeded = [&] {
        cancel_exclusive_request(request.generation_);
        request.router_ = nullptr;
        request.result_ = ExclusiveAcquireResult::DeadlineExceeded;
        return request.result_;
    };
    if (request.deadline_.has_value() &&
        std::chrono::steady_clock::now() >= *request.deadline_) {
        return deadline_exceeded();
    }
    std::unique_lock<std::mutex> lock(load_mutex_, std::defer_lock);
    if (request.deadline_.has_value()) {
        if (!lock.try_lock())
            return ExclusiveAcquireResult::Retry;
        if (std::chrono::steady_clock::now() >= *request.deadline_)
            return deadline_exceeded();
    } else {
        lock.lock();
    }
    if (exclusive_pending_generation_.load(std::memory_order_acquire) !=
        request.generation_) {
        request.router_ = nullptr;
        request.result_ = ExclusiveAcquireResult::InvalidOrder;
        return request.result_;
    }
    if (cancel && cancel->load()) {
        cancel_exclusive_request(request.generation_);
        request.router_ = nullptr;
        request.result_ = ExclusiveAcquireResult::Cancelled;
        return request.result_;
    }
    const bool lifecycle_mutation_active = std::any_of(
        pending_reload_states_.begin(),
        pending_reload_states_.end(),
        [](const auto& entry) {
            return entry.second.phase ==
                   PendingReloadPhase::Cancelled;
        });
    if (all_model_mutation_active_ || lifecycle_mutation_active ||
        is_loading_ || !prepared_load_counts_.empty()) {
        return ExclusiveAcquireResult::Retry;
    }
    const bool busy = std::any_of(
        loaded_servers_.begin(),
        loaded_servers_.end(),
        [](const auto& server) { return server->is_busy(); });
    if (busy) {
        return ExclusiveAcquireResult::Retry;
    }

    exclusive_active_ = true;
    exclusive_owner_ = std::this_thread::get_id();
    if (request.deadline_.has_value() &&
        std::chrono::steady_clock::now() >= *request.deadline_) {
        exclusive_active_ = false;
        exclusive_owner_ = std::thread::id{};
        return deadline_exceeded();
    }
    exclusive_pending_generation_.store(0, std::memory_order_release);
    request.router_ = nullptr;
    request.result_ = ExclusiveAcquireResult::Acquired;
    load_cv_.notify_all();
    return request.result_;
}

void Router::end_exclusive() {
    std::lock_guard<std::mutex> lock(load_mutex_);
    exclusive_active_ = false;
    exclusive_owner_ = std::thread::id{};
    load_cv_.notify_all();
}

void Router::wait_for_slot_clearance(std::unique_lock<std::mutex>& lock) {
    wait_for_load_state(lock, [&] {
        return !all_model_mutation_active_ &&
               !exclusive_pending() &&
               (!exclusive_active_ ||
                exclusive_owner_ == std::this_thread::get_id());
    });
}

std::string Router::resolve_model_name_after_mutation_gate_locked(
    std::unique_lock<std::mutex>& lock,
    const std::string& model_name) {
    while (true) {
        const std::string canonical_model_name = resolve_model_name(model_name);
        wait_for_slot_clearance(lock);
        wait_for_load_state(lock, [this, &canonical_model_name] {
            auto pending = pending_reload_states_.find(canonical_model_name);
            return !all_model_mutation_active_ &&
                   !exclusive_pending() &&
                   (!exclusive_active_ ||
                    exclusive_owner_ == std::this_thread::get_id()) &&
                   (pending == pending_reload_states_.end() ||
                    pending->second.phase != PendingReloadPhase::Cancelled);
        });
        if (resolve_model_name(model_name) == canonical_model_name) {
            return canonical_model_name;
        }
    }
}

std::map<std::string, bool> Router::snapshot_loaded_models() const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    std::map<std::string, bool> models;
    for (const auto& server : loaded_servers_)
        if (server->is_backend_alive()) models[server->get_model_name()] = server->is_pinned();
    return models;
}

std::map<std::string, json> Router::unload_job_models(const std::map<std::string, int>& owned_live,
                                                      const std::map<std::string, bool>& snapshot_pins) {
    std::unique_lock<std::mutex> lock(load_mutex_);
    wait_for_slot_clearance(lock);
    std::map<std::string, int> resolved_owned;
    for (const auto& kv : owned_live) resolved_owned[resolve_model_name(kv.first)] = kv.second;
    std::map<std::string, json> captured;
    std::vector<WrappedServer*> victims;
    for (const auto& server : loaded_servers_) {
        if (!server->is_backend_alive()) continue;
        const std::string name = server->get_model_name();
        auto pin_it = snapshot_pins.find(name);
        if (pin_it != snapshot_pins.end()) {
            if (server->is_pinned() != pin_it->second) server->set_pinned(pin_it->second);
            continue;
        }
        auto own_it = resolved_owned.find(name);
        if (own_it == resolved_owned.end()) continue;
        if (server->get_process_id() != own_it->second) continue;
        captured[name] = {{"options", server->get_recipe_options().to_json()},
                          {"pinned", server->is_pinned()}};
        victims.push_back(server.get());
    }
    for (auto* victim : victims) {
        if (victim->is_pinned()) victim->set_pinned(false);
        LOG(INFO, "Router") << "Reconcile-unload of job-loaded model: "
                            << victim->get_model_name() << std::endl;
        evict_server(victim);
    }
    return captured;
}

int Router::loaded_model_pid(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = find_server_by_model_name(resolve_model_name(model_name));
    return server && server->is_backend_alive() ? server->get_process_id() : -1;
}

std::string Router::canonical_model_name(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    return resolve_model_name(model_name);
}

bool Router::is_gpu_resident_server(const WrappedServer& server) const {
    return uses_gpu_memory_capacity(server.get_device_type());
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

double Router::sample_available_memory_gb(DeviceType device) const {
    if (!available_memory_sampler_) {
        return get_available_memory_gb(device);
    }
    try {
        return available_memory_sampler_(device);
    } catch (const std::exception& e) {
        LOG(WARNING, "Router") << "Failed to sample available memory: "
                                << e.what() << std::endl;
        return 0.0;
    }
}

double Router::get_lemonade_gpu_occupancy_gb() const {
    double total = 0.0;
    for (const auto& server : loaded_servers_) {
        if (is_gpu_resident_server(*server)) {
            total += std::max(0.0, server->get_gpu_memory_occupancy_gb());
        }
    }
    return total;
}

double Router::get_total_gpu_capacity_gb() const {
    json system_info = SystemInfoCache::get_system_info_with_cache();
    if (!system_info.contains("devices") || !system_info["devices"].is_object()) {
        return 0.0;
    }

    const json& devices = system_info["devices"];
    double single_device_capacity_gb = 0.0;

    auto accumulate_gpu_capacity = [&](const std::string& key) {
        if (!devices.contains(key)) return;
        json dev_list = devices[key].is_array() ? devices[key] : json::array({devices[key]});
        for (const auto& device : dev_list) {
            if (!device.is_object() || !device.value("available", false)) continue;
            const bool is_integrated_gpu = device.value("gpu_type", "") == "integrated";
            const double capacity_gb = gpu_memory_capacity_from_pools_gb(
                device.value("vram_gb", 0.0),
                device.value("virtual_mem_gb", 0.0),
                is_integrated_gpu,
                config_->enable_dgpu_gtt());
            single_device_capacity_gb = std::max(single_device_capacity_gb, capacity_gb);
        }
    };

    // Lemonade does not track per-request GPU placement, so do not treat memory
    // as pooled across cards. The sampler provides aggregate pressure; this
    // capacity is the largest single observable AMD device.
    accumulate_gpu_capacity("amd_gpu");

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
            GgufMetadata metadata;
            if (read_gguf_metadata(metadata, path) && metadata.block_count > 0) {
                int64_t ctx_size = options.get_option("ctx_size").get<int64_t>();
                if (ctx_size <= 0) {
                    ctx_size = model_info.max_context_window > 0 ? model_info.max_context_window : 4096;
                }
                if (model_info.type == ModelType::EMBEDDING && ctx_size < 8192) {
                    ctx_size = 8192;
                }
                double kv_bytes_per_token =
                    compute_weighted_kv_cache_bytes_per_token(metadata);
                if (kv_bytes_per_token <= 0.0) {
                    const int64_t key_length = metadata.key_length > 0
                        ? metadata.key_length
                        : (metadata.embedding_length > 0 ? metadata.embedding_length : 128);
                    const int64_t total_kv_heads = metadata.head_count_kv > 0
                        ? metadata.head_count_kv
                        : metadata.block_count;
                    kv_bytes_per_token =
                        static_cast<double>(total_kv_heads) *
                        static_cast<double>(key_length) *
                        2.0 *
                        2.0;
                }
                const double kv_bytes =
                    static_cast<double>(ctx_size) * kv_bytes_per_token;
                const double kv_gb = kv_bytes / (1024.0 * 1024.0 * 1024.0);
                return model_size_gb * 1.05 + kv_gb + 1.0;
            }
        }
    }

    return model_size_gb * 1.20 + 1.0;
}

GpuMemoryAdmissionPlan Router::plan_gpu_memory_capacity(
    const ModelInfo& model_info,
    const RecipeOptions& options,
    DeviceType effective_device,
    const WrappedServer* replacement_server) const {
    GpuMemoryAdmissionPlan no_op;
    if (!uses_gpu_memory_capacity(effective_device)) return no_op;

    const double total_capacity_gb = get_total_gpu_capacity_gb();
    if (total_capacity_gb <= 0.0) {
        LOG(DEBUG, "Router") << "Skipping GPU memory capacity check: total GPU capacity unavailable" << std::endl;
        return no_op;
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
        if (is_gpu_resident_server(*server) &&
            is_automatic_eviction_candidate(
                server->is_pinned(), server->is_busy())) {
            inputs.residents.push_back({
                server->get_model_name(),
                server->get_gpu_memory_occupancy_gb()
            });
        }
    }

    return plan_gpu_memory_admission(inputs);
}

void Router::load_prepared_model(
    PreparedModelLoad preparation,
    const ModelInfo& model_info,
    RecipeOptions options,
    bool do_not_upgrade,
    std::optional<bool> pinned,
    std::atomic<bool>* cancel_flag) {
    if (preparation.already_loaded_) {
        return;
    }
    load_model_impl(
        preparation, model_info, std::move(options), do_not_upgrade,
        pinned, cancel_flag);
}

void Router::load_model_impl(
    PreparedModelLoad& preparation,
    const ModelInfo& model_info,
    RecipeOptions options,
    bool do_not_upgrade,
    std::optional<bool> pinned,
    std::atomic<bool>* cancel_flag) {
    const std::string& canonical_model_name =
        preparation.canonical_model_name_;
    const auto load_generation = preparation.generation_;
    const ResidencyClass requested_residency_class =
        residency_class_for_load_purpose(preparation.load_purpose_);
    const RecipeOptions& requested_options =
        preparation.existing_model_policy_ ==
                ExistingModelPolicy::ReuseExisting &&
                preparation.watchdog_options_.has_value()
            ? *preparation.watchdog_options_
            : options;
    RecipeOptions effective_options =
        resolve_effective_options(model_info, requested_options);

    // LOAD SERIALIZATION STRATEGY (from spec: point #2 in Additional Considerations)
    std::unique_lock<std::mutex> lock(load_mutex_);

    wait_for_load_state(lock, [&] {
        const auto pending_generation =
            exclusive_pending_generation_.load(std::memory_order_acquire);
        const bool claim_invalid = !reload_generation_matches_locked(
            canonical_model_name,
            load_generation,
            model_info,
            preparation.existing_model_policy_);
        const bool admitted_before_exclusive =
            pending_generation != 0 &&
            load_generation <= exclusive_admission_generation_;
        return claim_invalid ||
               (!is_loading_ &&
                (!exclusive_active_ ||
                 exclusive_owner_ == std::this_thread::get_id()) &&
                (pending_generation == 0 || admitted_before_exclusive));
    });

    if (!reload_generation_matches_locked(
            canonical_model_name,
            load_generation,
            model_info,
            preparation.existing_model_policy_)) {
        throw std::runtime_error(
            "Model load was cancelled or superseded for " +
            canonical_model_name);
    }

    is_loading_ = true;
    active_load_model_name_ = canonical_model_name;

    LOG(DEBUG, "Router") << "Loading model: " << canonical_model_name
            << " (checkpoint: " << model_info.checkpoint()
            << ", recipe: " << model_info.recipe
            << ", type: " << model_type_to_string(model_info.type)
            << ", device: " << device_type_to_string(model_info.device)
            << ", residency: "
            << residency_class_to_string(requested_residency_class) << ")" << std::endl;

    try {
        WrappedServer* reload_existing = nullptr;
        WrappedServer* existing_pre =
            find_server_by_model_name(canonical_model_name);
        auto pending_reload = pending_reload_states_.find(canonical_model_name);
        const auto publication_generation = load_generation;

        auto publication_generation_is_current = [&] {
            return reload_generation_matches_locked(
                canonical_model_name,
                publication_generation,
                model_info,
                preparation.existing_model_policy_);
        };
        auto finish_loading = [&] {
            active_load_model_name_.clear();
            is_loading_ = false;
            load_cv_.notify_all();
        };
        auto complete_bound_reload = [&] {
            complete_reload_generation_locked(
                canonical_model_name, load_generation);
        };

        pending_reload = pending_reload_states_.find(canonical_model_name);
        const json option_pinned = effective_options.get_option("pinned");
        bool retained_pinned = false;
        if (existing_pre) {
            retained_pinned = existing_pre->is_pinned();
        } else if (pending_reload != pending_reload_states_.end()) {
            retained_pinned = pending_reload->second.pinned.value_or(false);
        }
        const bool final_pinned = pinned.value_or(
            retained_pinned || is_config_pinned(canonical_model_name) ||
            (option_pinned.is_boolean() && option_pinned.get<bool>()));

        if (routing_helper_no_longer_needed(canonical_model_name,
                                            requested_residency_class, final_pinned)) {
            LOG(INFO, "Router") << "Skipping load of routing helper "
                                << canonical_model_name
                                << "; referenced by no active policy" << std::endl;
            complete_bound_reload();
            finish_loading();
            return;
        }

        auto options_match = [&](const WrappedServer& existing) {
            json existing_opts = existing.get_recipe_options().to_resolved_json();
            json requested_opts = effective_options.to_resolved_json();
            existing_opts.erase("pinned");
            requested_opts.erase("pinned");
            if (existing.ctx_size_is_auto() &&
                requested_opts.value("ctx_size", json(nullptr)) == -1) {
                existing_opts.erase("ctx_size");
                requested_opts.erase("ctx_size");
            }
            return existing_opts == requested_opts;
        };

        if (existing_pre && existing_pre->is_backend_alive() &&
            (preparation.existing_model_policy_ !=
                 ExistingModelPolicy::ReconcileOptions ||
             options_match(*existing_pre))) {
            transition_server_residency_locked(
                existing_pre, requested_residency_class);
            LOG(DEBUG, "Router")
                << "Model already loaded, updating access time and pinned status"
                << std::endl;
            existing_pre->set_pinned(final_pinned);
            existing_pre->update_access_time();
            complete_bound_reload();
            finish_loading();
            return;
        }

        std::unique_ptr<WrappedServer> new_server = create_backend_server(model_info);
        ModelType model_type = model_info.type;
        DeviceType device_type = new_server->effective_device(effective_options);
        std::optional<int64_t> bound_npu_auto_ctx;

        if (device_type & DEVICE_NPU) {
            ModelInfo preflight_info = model_info;
            preflight_info.device = device_type;
            const double available_memory_gb =
                sample_available_memory_gb(device_type);
            bound_npu_auto_ctx = resolve_auto_ctx_size(
                effective_options, preflight_info, available_memory_gb);
            if (*bound_npu_auto_ctx == AUTO_CTX_FALLBACK &&
                model_info.size > 10.0 && available_memory_gb > 0) {
                throw std::runtime_error(
                    "Not enough memory to load " + canonical_model_name +
                    " (" +
                    std::to_string(static_cast<int>(model_info.size)) +
                    " GB). The NPU driver needs additional working memory beyond" +
                    " model weights. Free up memory or try a smaller model.");
            }
        }

        prune_unavailable_servers_locked();
        prune_stale_routing_helpers_locked();

        if (pinned.has_value()) {
            auto retained = pending_reload_states_.find(canonical_model_name);
            if (retained != pending_reload_states_.end() &&
                retained->second.phase == PendingReloadPhase::Pending) {
                retained->second.pinned = *pinned;
            }
        }

        WrappedServer* existing = find_server_by_model_name(canonical_model_name);
        if (existing && !existing->is_backend_alive()) {
            LOG(WARNING, "Router") << "Existing backend for " << canonical_model_name
                                    << " is unavailable (state="
                                    << existing->get_backend_health_state()
                                    << "), evicting before reload" << std::endl;
            evict_server(existing);
            if (find_server_by_model_name(canonical_model_name) == existing) {
                throw std::runtime_error(
                    "Backend for " + canonical_model_name +
                    " is still releasing an interrupted request; retry the load");
            }
            existing = nullptr;
        }
        if (existing) {
            if (preparation.existing_model_policy_ ==
                    ExistingModelPolicy::ReconcileOptions &&
                !options_match(*existing)) {
                LOG(INFO, "Router") << "Options changed, reloading model: "
                                    << canonical_model_name << std::endl;
                reload_existing = existing;
            } else {
                transition_server_residency_locked(
                    existing, requested_residency_class);
                existing->set_pinned(final_pinned);
                existing->update_access_time();
                complete_bound_reload();
                finish_loading();
                return;
            }
        }

        // NPU admission is a preflight-and-commit operation. Compute the complete
        // conflict set first, then reject any protected resident before changing
        // residency. A pinned incoming model never grants permission to evict a
        // pinned or in-use incumbent.
        const SlotPolicy requested_slot_policy =
            new_server->effective_slot_policy(effective_options);
        std::vector<AdmissionEvictionCandidate> admission_candidates;
        for (const auto& server : loaded_servers_) {
            if (server.get() == reload_existing || !server->is_backend_alive() ||
                !(server->get_device_type() & DEVICE_NPU)) {
                continue;
            }

            const SlotPolicy resident_slot_policy = server->effective_slot_policy(
                server->get_recipe_options());
            const bool conflicts_with_exclusive =
                requested_slot_policy == SlotPolicy::ExclusiveNpu;
            const bool conflicts_with_coexisting =
                requested_slot_policy == SlotPolicy::CoexistByType &&
                (resident_slot_policy != SlotPolicy::CoexistByType ||
                 server->get_model_type() == model_type);
            if (conflicts_with_exclusive || conflicts_with_coexisting) {
                if (should_reject_residency_displacement(
                        requested_residency_class,
                        server->get_residency_class())) {
                    throw RouterResidencyConflictException(
                        canonical_model_name,
                        server->get_model_name(),
                        requested_slot_policy == SlotPolicy::ExclusiveNpu
                            ? model_info.recipe + " requires exclusive NPU access"
                            : "FLM cannot displace the resident NPU residency class");
                }
                admission_candidates.push_back({
                    server->get_model_name(), server->is_pinned(), server->is_busy()});
            }
        }

        const bool requested_gpu = uses_gpu_memory_capacity(device_type);
        GpuMemoryAdmissionPlan gpu_plan = plan_gpu_memory_capacity(
            model_info, effective_options, device_type, reload_existing);
        if (!gpu_plan.can_fit) {
            throw std::runtime_error(gpu_plan.rejection_reason);
        }
        for (const auto& model_to_evict : gpu_plan.models_to_evict) {
            WrappedServer* server = find_server_by_model_name(model_to_evict);
            if (server) {
                admission_candidates.push_back({
                    server->get_model_name(), server->is_pinned(), server->is_busy()});
            }
        }

        if (reload_existing) {
            admission_candidates.push_back({
                reload_existing->get_model_name(), false, reload_existing->is_busy()});
        }

        const bool is_unmetered_load = is_unmetered_recipe(model_info.recipe);
        if (!is_unmetered_load) {
            const int limit = residency_limit(
                requested_residency_class, config_->max_loaded_models());
            const int resident_count = count_servers_in_pool(
                model_type, requested_residency_class, canonical_model_name);
            bool planned_victim_frees_count_slot = false;
            for (const auto& candidate : admission_candidates) {
                WrappedServer* server = find_server_by_model_name(candidate.model_name);
                if (server && server->is_backend_alive() &&
                    same_residency_pool(
                        server->get_model_type(), server->get_residency_class(),
                        server->get_model_name(), model_type,
                        requested_residency_class, canonical_model_name)) {
                    planned_victim_frees_count_slot = true;
                    break;
                }
            }

            if (limit != -1 && resident_count >= limit &&
                !planned_victim_frees_count_slot) {
                WrappedServer* count_victim = find_lru_server_in_pool(
                    model_type, requested_residency_class, canonical_model_name);
                if (!count_victim) {
                    throw SlotsPinnedException(residency_pool_to_string(
                        model_type, requested_residency_class));
                }
                admission_candidates.push_back({
                    count_victim->get_model_name(), count_victim->is_pinned(),
                    count_victim->is_busy()});
            }
        }

        AdmissionEvictionPlan admission_plan = plan_admission_evictions(
            admission_candidates);
        if (!admission_plan.can_admit) {
            const char* protection =
                admission_plan.blocked_reason == AdmissionBlockReason::Pinned
                    ? "Pinned"
                    : "In-use";
            throw std::runtime_error(
                std::string(protection) +
                " model cannot be evicted for admission: " +
                admission_plan.blocked_model);
        }

        for (const auto& model_to_evict : admission_plan.models_to_evict) {
            WrappedServer* victim = find_server_by_model_name(model_to_evict);
            if (!victim) {
                continue;
            }
            LOG(INFO, "Router") << "Admission requires evicting "
                                << victim->get_model_name() << std::endl;
            evict_server(victim);
        }

        if (!gpu_plan.models_to_evict.empty()) {
            LOG(INFO, "Router") << "GPU memory budget projected occupancy after eviction: "
                                << gpu_plan.projected_occupancy_gb << " GB / "
                                << gpu_plan.effective_capacity_gb << " GB" << std::endl;
        }

        if (reload_existing &&
            find_server_by_model_name(canonical_model_name)) {
            throw std::runtime_error(
                "Failed to evict existing model before reload: " +
                canonical_model_name);
        }

        // NPU auto-context is bound by the pre-mutation admission check. Other
        // devices may sample again after eviction so released memory is visible.
        ModelInfo auto_tune_info = model_info;
        auto_tune_info.device = device_type;
        const int64_t auto_ctx = bound_npu_auto_ctx
            ? *bound_npu_auto_ctx
            : resolve_auto_ctx_size(
                  effective_options, auto_tune_info,
                  sample_available_memory_gb(device_type));
        const bool ctx_size_auto = auto_ctx != -2;
        if (auto_ctx > 0) {
            LOG(INFO, "Router") << "Auto-tune ctx_size resolved to " << auto_ctx << std::endl;
            effective_options.set_option("ctx_size", auto_ctx);
        }

        LOG(DEBUG, "Router") << "Effective settings: " << effective_options.to_log_string() << std::endl;

        // Set model metadata
        new_server->set_model_metadata(canonical_model_name, model_info.checkpoint(), model_type, device_type, effective_options);
        new_server->set_ctx_size_auto(ctx_size_auto);
        new_server->set_residency_class(requested_residency_class);
        new_server->set_pinned(final_pinned);
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
        auto load_start = std::chrono::steady_clock::now();

        new_server->set_load_cancel_flag(cancel_flag);

        try {
            new_server->load(canonical_model_name, model_info, effective_options, do_not_upgrade);
            load_success = true;
            auto load_end = std::chrono::steady_clock::now();
            new_server->set_load_duration_ms(std::chrono::duration_cast<std::chrono::milliseconds>(load_end - load_start).count());
            LOG(DEBUG, "Router") << "Backend started successfully in " << new_server->get_load_duration_ms() << "ms" << std::endl;
        } catch (const std::exception& e) {
            error_message = e.what();
            load_success = false;
            LOG(ERROR, "Router") << "Backend load failed: " << error_message << std::endl;
        }

        new_server->set_load_cancel_flag(nullptr);

        lock.lock();

        if (!publication_generation_is_current()) {
            if (load_success) {
                new_server->unload();
            }
            finish_loading();
            throw std::runtime_error(
                "Model load was cancelled or superseded for " +
                canonical_model_name);
        }

        if (load_success) {
            // A policy change may have dropped this helper while its backend was
            // starting (the load ran with load_mutex_ released). Now that we hold
            // the lock again, validate against the authoritative needed set so a
            // helper no active policy references is never committed.
            if (routing_helper_no_longer_needed(canonical_model_name,
                                                requested_residency_class,
                                                new_server->is_pinned())) {
                LOG(INFO, "Router") << "Routing helper " << canonical_model_name
                          << " no longer referenced by any active policy; "
                          << "discarding freshly loaded backend" << std::endl;
                new_server->unload();
                complete_bound_reload();
                finish_loading();
                return;
            }

            // Success: Refresh access time so this model is returned by
            // get_most_recent_server() (the pre-load timestamp from line 316
            // may have been overtaken by other models serving requests while
            // the lock was released during the slow backend load).
            new_server->update_access_time();
            new_server->set_state(ModelState::READY);

            // Record the observed allocation for future capacity planning. The
            // prediction remains the conservative fallback when the platform
            // sampler cannot isolate a positive load delta.
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

            // Add to loaded servers.
            install_reclaim_notifier(new_server.get());
            loaded_servers_.push_back(std::move(new_server));
            complete_bound_reload();
            finish_loading();

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

            if (cancel_flag && cancel_flag->load()) {
                LOG(INFO, "Router") << "Load cancelled, skipping nuclear retry" << std::endl;
                throw std::runtime_error("load cancelled");
            }

            if (is_file_not_found) {
                LOG(ERROR, "Router") << "File not found error, NOT evicting other models" << std::endl;
                throw std::runtime_error(error_message);
            }

            // A policy change may have dropped this helper during the failed
            // load. Don't unleash the nuclear eviction on behalf of a backend we
            // would immediately discard at commit time anyway.
            if (routing_helper_no_longer_needed(canonical_model_name,
                                                requested_residency_class, final_pinned)) {
                LOG(INFO, "Router") << "Routing helper " << canonical_model_name
                          << " no longer referenced by any active policy; "
                          << "abandoning nuclear retry" << std::endl;
                complete_bound_reload();
                finish_loading();
                return;
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
            retry_server->set_ctx_size_auto(ctx_size_auto);
            retry_server->set_residency_class(requested_residency_class);
            retry_server->set_pinned(final_pinned);
            retry_server->update_access_time();
            retry_server->set_load_cancel_flag(cancel_flag);
            const double retry_gpu_occupancy_before_load =
                requested_gpu ? sample_total_gpu_occupancy_gb() : -1.0;

            lock.unlock();

            LOG(DEBUG, "Router") << "Retrying backend load..." << std::endl;
            try {
                auto retry_start = std::chrono::steady_clock::now();
                retry_server->load(canonical_model_name, model_info, effective_options, do_not_upgrade);
                auto retry_end = std::chrono::steady_clock::now();
                retry_server->set_load_duration_ms(std::chrono::duration_cast<std::chrono::milliseconds>(retry_end - retry_start).count());

                lock.lock();

                retry_server->set_load_cancel_flag(nullptr);

                if (!publication_generation_is_current()) {
                    retry_server->unload();
                    finish_loading();
                    throw std::runtime_error(
                        "Model load was cancelled or superseded for " +
                        canonical_model_name);
                }

                // Same policy-churn guard as the initial load: a helper the
                // active policy dropped while this retry backend was starting
                // must be discarded, not committed.
                if (routing_helper_no_longer_needed(canonical_model_name,
                                                    requested_residency_class,
                                                    retry_server->is_pinned())) {
                    LOG(INFO, "Router") << "Routing helper " << canonical_model_name
                              << " no longer referenced by any active policy; "
                              << "discarding freshly loaded backend" << std::endl;
                    retry_server->unload();
                    complete_bound_reload();
                    finish_loading();
                    return;
                }

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

                retry_server->set_state(ModelState::READY);
                const auto retry_duration_ms = retry_server->get_load_duration_ms();
                install_reclaim_notifier(retry_server.get());
                loaded_servers_.push_back(std::move(retry_server));
                complete_bound_reload();
                finish_loading();

                LOG(DEBUG, "Router") << "Retry successful in " << retry_duration_ms << "ms!" << std::endl;
            } catch (const std::exception& retry_error) {
                retry_server->set_load_cancel_flag(nullptr);
                if (!lock.owns_lock()) {
                    lock.lock();
                }
                finish_loading();

                LOG(ERROR, "Router") << "Retry also failed: " << retry_error.what() << std::endl;
                throw;
            }
        }

    } catch (const std::exception& e) {
        LOG(ERROR, "Router") << "Failed to load model: " << e.what() << std::endl;

        if (!lock.owns_lock()) {
            lock.lock();
        }
        active_load_model_name_.clear();
        is_loading_ = false;
        load_cv_.notify_all();

        throw;
    }
}

bool Router::discard_model_runtime_state_locked(
    std::unique_lock<std::mutex>& lock,
    const std::string& canonical_model_name) {
    wait_for_slot_clearance(lock);
    load_cv_.wait(lock, [this, &canonical_model_name] {
        auto pending = pending_reload_states_.find(canonical_model_name);
        return !all_model_mutation_active_ &&
               (!exclusive_active_ ||
                exclusive_owner_ == std::this_thread::get_id()) &&
               (pending == pending_reload_states_.end() ||
                pending->second.phase != PendingReloadPhase::Cancelled);
    });
    const bool had_runtime_state =
        find_server_by_model_name(canonical_model_name) != nullptr ||
        pending_reload_states_.find(canonical_model_name) !=
            pending_reload_states_.end() ||
        prepared_load_counts_.find(canonical_model_name) !=
            prepared_load_counts_.end() ||
        active_load_model_name_ == canonical_model_name;

    const auto generation =
        cancel_pending_reload_locked(canonical_model_name);
    auto clear_cancellation = [this, &canonical_model_name, generation] {
        auto pending = pending_reload_states_.find(canonical_model_name);
        if (pending != pending_reload_states_.end() &&
            pending->second.generation == generation &&
            pending->second.phase == PendingReloadPhase::Cancelled) {
            pending_reload_states_.erase(pending);
        }
        load_cv_.notify_all();
    };
    load_cv_.wait(lock, [this, &canonical_model_name] {
        return active_load_model_name_ != canonical_model_name &&
               prepared_load_counts_.find(canonical_model_name) ==
                   prepared_load_counts_.end() &&
               (!exclusive_active_ ||
                exclusive_owner_ == std::this_thread::get_id());
    });

    try {
        WrappedServer* server =
            find_server_by_model_name(canonical_model_name);
        if (server) {
            evict_server(server);
        }
        if (find_server_by_model_name(canonical_model_name)) {
            throw std::runtime_error(
                "Model runtime state is still in use: " +
                canonical_model_name);
        }
    } catch (...) {
        clear_cancellation();
        throw;
    }

    clear_cancellation();
    return had_runtime_state;
}

Router::ModelRuntimeMutation Router::begin_model_runtime_mutation(
    const std::string& model_name) {
    std::unique_lock<std::mutex> lock(load_mutex_);
    const std::string canonical_model_name =
        resolve_model_name_after_mutation_gate_locked(lock, model_name);

    const auto generation =
        cancel_pending_reload_locked(canonical_model_name);
    auto clear_cancellation = [this, &canonical_model_name, generation] {
        auto pending = pending_reload_states_.find(canonical_model_name);
        if (pending != pending_reload_states_.end() &&
            pending->second.generation == generation &&
            pending->second.phase == PendingReloadPhase::Cancelled) {
            pending_reload_states_.erase(pending);
        }
        load_cv_.notify_all();
    };
    load_cv_.wait(lock, [this, &canonical_model_name] {
        return active_load_model_name_ != canonical_model_name &&
               prepared_load_counts_.find(canonical_model_name) ==
                   prepared_load_counts_.end() &&
               (!exclusive_active_ ||
                exclusive_owner_ == std::this_thread::get_id());
    });

    try {
        if (auto* server =
                find_server_by_model_name(canonical_model_name)) {
            evict_server(server);
        }
        if (find_server_by_model_name(canonical_model_name)) {
            throw std::runtime_error(
                "Model runtime state is still in use: " +
                canonical_model_name);
        }

        return ModelRuntimeMutation(
            this, canonical_model_name, generation);
    } catch (...) {
        clear_cancellation();
        throw;
    }
}

void Router::unload_model(const std::string& model_name) {
    std::unique_lock<std::mutex> lock(load_mutex_);

    if (model_name.empty()) {
        LOG(INFO, "Router") << "Unload all models called" << std::endl;
        wait_for_slot_clearance(lock);
        load_cv_.wait(lock, [this] {
            return !all_model_mutation_active_ &&
                   (!exclusive_active_ ||
                    exclusive_owner_ == std::this_thread::get_id()) &&
                   std::none_of(
                pending_reload_states_.begin(),
                pending_reload_states_.end(),
                [](const auto& entry) {
                    return entry.second.phase ==
                           PendingReloadPhase::Cancelled;
                });
        });
        all_model_mutation_active_ = true;
        std::set<std::string> model_names;
        for (const auto& server : loaded_servers_) {
            model_names.insert(server->get_model_name());
        }
        for (const auto& [pending_name, pending] : pending_reload_states_) {
            (void)pending;
            model_names.insert(pending_name);
        }
        for (const auto& [prepared_name, count] : prepared_load_counts_) {
            (void)count;
            model_names.insert(prepared_name);
        }
        if (!active_load_model_name_.empty()) {
            model_names.insert(active_load_model_name_);
        }
        std::map<std::string, uint64_t> cancellation_generations;
        auto finish_all_model_mutation = [&] {
            for (const auto& [name, generation] : cancellation_generations) {
                auto pending = pending_reload_states_.find(name);
                if (pending != pending_reload_states_.end() &&
                    pending->second.phase == PendingReloadPhase::Cancelled &&
                    pending->second.generation == generation) {
                    pending_reload_states_.erase(pending);
                }
            }
            all_model_mutation_active_ = false;
            load_cv_.notify_all();
        };
        try {
            for (const auto& name : model_names) {
                cancellation_generations.emplace(
                    name, cancel_pending_reload_locked(name));
            }
            load_cv_.wait(lock, [this] {
                return !is_loading_ && prepared_load_counts_.empty() &&
                       (!exclusive_active_ ||
                        exclusive_owner_ == std::this_thread::get_id());
            });
            evict_all_servers(/*include_pinned=*/true);
            if (!loaded_servers_.empty()) {
                throw std::runtime_error(
                    "One or more model runtimes are still in use");
            }
            finish_all_model_mutation();
        } catch (...) {
            finish_all_model_mutation();
            throw;
        }
    } else {
        LOG(INFO, "Router") << "Unload model called: " << model_name << std::endl;
        const std::string canonical_model_name =
            resolve_model_name_after_mutation_gate_locked(lock, model_name);
        if (!discard_model_runtime_state_locked(lock, canonical_model_name)) {
            throw std::runtime_error("Model not loaded: " + model_name);
        }
    }
}

void Router::evict_if_committed(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(load_mutex_);

    WrappedServer* server = find_server_by_model_name(model_name);
    if (!server) {
        return;  // Already gone
    }

    // An exclusive session may have started (and re-pinned this model via the
    // job snapshot reconcile) since the EVICTING mark was set. Neither state
    // was known when the eviction was decided, so abandon it.
    if (exclusive_active_ || exclusive_pending() || server->is_pinned()) {
        server->rescue_from_eviction();
        LOG(INFO, "Router") << "Eviction of " << model_name << " cancelled ("
                            << (server->is_pinned() ? "pinned" : "exclusive session active")
                            << ")" << std::endl;
        return;
    }

    // Atomically confirm the model is still idle and EVICTING. If a request
    // rescued it (now IN_USE) this returns false and reverts it to READY, so we
    // leave it loaded — no crashed generation, no talking to a dead subprocess.
    if (!server->try_commit_eviction()) {
        LOG(INFO, "Router") << "Eviction of " << model_name
                            << " cancelled (rescued by in-flight request)" << std::endl;
        return;
    }

    evict_server(server);
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

std::string Router::get_sole_loaded_model_of_type(ModelType type) const {
    std::lock_guard<std::mutex> lock(load_mutex_);

    WrappedServer* standard_match = nullptr;
    WrappedServer* helper_match = nullptr;
    bool helper_ambiguous = false;
    for (const auto& server : loaded_servers_) {
        if (!server->is_backend_alive() || server->get_model_type() != type) {
            continue;
        }
        if (server->get_residency_class() == ResidencyClass::Standard) {
            if (standard_match) return "";  // multiple user-facing models
            standard_match = server.get();
        } else if (helper_match) {
            helper_ambiguous = true;
        } else {
            helper_match = server.get();
        }
    }

    // A single standard model remains an unambiguous default even while an
    // internal router/classifier model of the same ModelType is resident.
    WrappedServer* match = standard_match
        ? standard_match
        : (helper_ambiguous ? nullptr : helper_match);
    return match ? model_manager_->get_public_model_name(match->get_model_name()) : "";
}

json Router::get_all_loaded_models() const {
    std::lock_guard<std::mutex> lock(load_mutex_);

    json result = json::array();

    for (const auto& server : loaded_servers_) {
        const bool backend_alive = server->is_backend_alive();
        if (!backend_alive) {
            continue;
        }
        json model_info;
        model_info["model_name"] = model_manager_->get_public_model_name(server->get_model_name());
        model_info["checkpoint"] = server->get_checkpoint();
        model_info["type"] = model_type_to_string(server->get_model_type());
        model_info["residency_class"] = residency_class_to_string(server->get_residency_class());
        model_info["slot_pool"] = is_unmetered_recipe(
            server->get_recipe_options().get_recipe())
            ? "unmetered"
            : residency_pool_to_string(
                  server->get_model_type(), server->get_residency_class());
        model_info["device"] = device_type_to_string(server->get_device_type());
        model_info["backend_url"] = server->get_address();  // For debugging port issues
        model_info["pid"] = server->get_process_id();
        model_info["gpu_memory_occupancy_gb"] = server->get_gpu_memory_occupancy_gb();
        model_info["status"] = model_state_to_string(server->get_state());
        model_info["backend_alive"] = true;
        model_info["backend_health"] = server->get_backend_health_state();
        model_info["loaded"] = true;
        model_info["watchdog_reset"] = server->was_watchdog_triggered();
        std::string watchdog_reason = server->get_watchdog_reset_reason();
        if (!watchdog_reason.empty()) {
            model_info["watchdog_reset_reason"] = watchdog_reason;
        }
        model_info["pinned"] = server->is_pinned();
        RecipeOptions recipe_options =  server->get_recipe_options();
        model_info["recipe"] = recipe_options.get_recipe();
        model_info["recipe_options"] = recipe_options.to_json();
        model_info["is_busy"] = server->is_busy();
        model_info["is_streaming"] = server->is_streaming();

        // Static metadata from the registry entry. Cloud models carry the
        // provider-reported context window + per-million-token cost (recorded
        // at discovery by ModelManager::refresh_cloud_models); local models
        // surface their runtime context via recipe_options instead.
        try {
            const ModelInfo reg_info = model_manager_->get_model_info(server->get_model_name());
            if (reg_info.max_context_window > 0) {
                model_info["max_context_window"] = reg_info.max_context_window;
            }
            if (reg_info.cost_input_per_million >= 0) {
                model_info["cost_input_per_million"] = reg_info.cost_input_per_million;
            }
            if (reg_info.cost_output_per_million >= 0) {
                model_info["cost_output_per_million"] = reg_info.cost_output_per_million;
            }
        } catch (...) {
            // Registry entry not found (raced with a delete) — skip static metadata.
        }

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
        {"transcription", max},
        {"image", max},
        {"tts", max},
        {"classification", max}
    };
}

bool Router::is_model_loaded() const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    for (const auto& server : loaded_servers_) {
        if (server->is_backend_alive()) {
            return true;
        }
    }
    return false;
}

bool Router::is_model_loaded(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    auto* server = find_server_by_model_name(resolve_model_name(model_name));
    return server != nullptr && server->is_backend_alive();
}

bool Router::is_model_tracked(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    const std::string canonical_model_name = resolve_model_name(model_name);
    auto pending = pending_reload_states_.find(canonical_model_name);
    return find_server_by_model_name(canonical_model_name) != nullptr ||
           (pending != pending_reload_states_.end() &&
            pending->second.phase == PendingReloadPhase::Pending) ||
           active_load_model_name_ == canonical_model_name;
}

RecipeOptions Router::resolve_effective_options(const ModelInfo& model_info,
                                                const RecipeOptions& request_options) const {
    const std::string backend_option = model_info.recipe + "_backend";

    RecipeOptions tentative = request_options.inherit(model_info.recipe_options.inherit(
        RecipeOptions(model_info.recipe, config_->recipe_options(""))));
    json backend_json = tentative.get_option(backend_option);
    const std::string backend = backend_json.is_string() ? backend_json.get<std::string>() : "";

    RecipeOptions default_opt = RecipeOptions(model_info.recipe, config_->recipe_options(backend));
    RecipeOptions arch_opts(
        model_info.recipe,
        model_manager_
            ? model_manager_->get_architecture_defaults(model_info.gguf.architecture)
            : json::object());
    return request_options.inherit(model_info.recipe_options.inherit(arch_opts.inherit(default_opt)));
}

RecipeOptions Router::get_model_recipe_options(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    auto* server = find_server_by_model_name(resolve_model_name(model_name));
    if (server && server->is_backend_alive()) return server->get_recipe_options();
    return RecipeOptions();
}

ModelType Router::get_model_type(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = model_name.empty()
        ? get_most_recent_server()
        : find_server_by_model_name(resolve_model_name(model_name));
    return (server && server->is_backend_alive()) ? server->get_model_type() : ModelType::LLM;
}

std::string Router::get_backend_address() const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = get_most_recent_server();
    return (server && server->is_backend_alive()) ? server->get_address() : "";
}

std::string Router::get_streaming_transcription_address(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    WrappedServer* server = nullptr;
    if (!model_name.empty()) {
        // Route by the session's requested model, like the normal inference
        // path — most-recent would misroute multi-model setups (e.g. a
        // Whisper session connecting to Moonshine's stream)
        server = find_server_by_model_name(resolve_model_name(model_name));
    } else {
        server = get_most_recent_server();
    }
    if (!server) {
        return "";
    }
    auto* streaming = dynamic_cast<IStreamingTranscriptionServer*>(server);
    return streaming ? streaming->get_streaming_address() : "";
}

template<typename Func>
auto Router::execute_inference(const json& request, Func&& inference_func) -> decltype(inference_func(nullptr)) {
    std::string requested_model;
    if (request.contains("model") && request["model"].is_string()) {
        requested_model = request["model"].get<std::string>();
    }

    if (requested_model.empty()) {
        return ErrorResponse::from_exception(InvalidRequestException("No model specified in request"));
    }

    // A watchdog reset should be transparent for non-streaming calls when the
    // backend died before any response was returned. Retry exactly once after a
    // lazy reload; streaming paths deliberately do not retry after partial data.
    for (int attempt = 0; attempt < 2; ++attempt) {
        WrappedServer* server = nullptr;
        RecipeOptions restart_options;
        std::string restart_model_name;
        bool should_reload_before_request = false;

        {
            std::unique_lock<std::mutex> lock(load_mutex_);
            wait_for_slot_clearance(lock);
            server = find_server_by_model_name(resolve_model_name(requested_model));
            if (!server) {
                return ErrorResponse::from_exception(ModelNotLoadedException(requested_model));
            }

            if (!server->is_backend_alive()) {
                restart_options = server->get_recipe_options();
                restart_model_name = server->get_model_name();
                should_reload_before_request = true;
            } else {
                if (!server->acquire_for_inference()) {
                    return ErrorResponse::from_exception(ModelNotLoadedException(requested_model));
                }
                server->update_access_time();
            }
        } // Lock released here

        if (should_reload_before_request) {
            if (restart_model_name.empty()) {
                restart_model_name = requested_model;
            }
            if (attempt == 0 && reload_model_after_watchdog_reset(restart_model_name, restart_options)) {
                continue;
            }
            return ErrorResponse::create(
                "Backend for model '" + requested_model + "' is unavailable",
                ErrorType::BACKEND_ERROR,
                {{"code", "backend_unavailable"}, {"retryable", true}}
            );
        }

        InhibitGuard inhibit_guard(suspend_inhibitor_.get(), config_->inhibit_suspend());

        try {
            auto response = inference_func(server);
            const bool watchdog_reset =
                server->was_watchdog_triggered() || is_watchdog_reset_response(response);

            if (attempt == 0 && watchdog_reset) {
                restart_options = server->get_recipe_options();
                restart_model_name = server->get_model_name();
            }

            server->release_inference();

            if (attempt == 0 && watchdog_reset) {
                if (restart_model_name.empty()) {
                    restart_model_name = requested_model;
                }
                if (reload_model_after_watchdog_reset(restart_model_name, restart_options)) {
                    continue;
                }
            }

            return response;
        } catch (...) {
            server->release_inference();
            throw;
        }
    }

    return ErrorResponse::create(
        "Backend watchdog reset recovery failed for model '" + requested_model + "'",
        ErrorType::BACKEND_ERROR,
        {{"code", "backend_watchdog_reset"}, {"retryable", true}}
    );
}

// Template method for streaming execution
template<typename Func>
void Router::execute_streaming(const std::string& request_body, httplib::DataSink& sink, Func&& streaming_func, std::shared_ptr<telemetry::InferenceSpan> span) {
    WrappedServer* server = nullptr;
    std::string requested_model;

    try {
        json request = json::parse(request_body);
        if (request.contains("model") && request["model"].is_string()) {
            requested_model = request["model"].get<std::string>();
        }
    } catch (...) {
        LOG(DEBUG, "Router") << "Failed to parse request body for model extraction" << std::endl;
    }

    if (requested_model.empty()) {
        LOG(ERROR, "Router") << "No model specified in streaming request" << std::endl;
        json error = ErrorResponse::from_exception(InvalidRequestException("No model specified in request"));
        std::string error_msg = "data: " + error.dump() + "\n\n";
        sink.write(error_msg.c_str(), error_msg.size());
        sink.done();
        return;
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        RecipeOptions restart_options;
        std::string restart_model_name;
        bool should_reload_before_request = false;

        {
            std::unique_lock<std::mutex> lock(load_mutex_);
            wait_for_slot_clearance(lock);
            server = find_server_by_model_name(resolve_model_name(requested_model));
            if (!server) {
                json error = ErrorResponse::from_exception(ModelNotLoadedException(requested_model));
                std::string error_msg = "data: " + error.dump() + "\n\n";
                sink.write(error_msg.c_str(), error_msg.size());
                sink.done();
                return;
            }

            if (!server->is_backend_alive()) {
                restart_options = server->get_recipe_options();
                restart_model_name = server->get_model_name();
                should_reload_before_request = true;
            } else {
                if (!server->acquire_for_inference()) {
                    std::string error_msg =
                        "data: {\"error\":{\"message\":\"Model evicted: " + requested_model +
                        "\",\"type\":\"model_not_loaded\"}}\n\n";
                    sink.write(error_msg.c_str(), error_msg.size());
                    sink.done();
                    return;
                }
                server->update_access_time();
            }
        } // Lock released here

        if (should_reload_before_request) {
            if (restart_model_name.empty()) {
                restart_model_name = requested_model;
            }
            if (attempt == 0 && reload_model_after_watchdog_reset(restart_model_name, restart_options)) {
                continue;
            }

            json error = ErrorResponse::create(
                "Backend for model '" + requested_model + "' is unavailable",
                ErrorType::BACKEND_ERROR,
                {{"code", "backend_unavailable"}, {"retryable", true}}
            );
            std::string error_msg = "data: " + error.dump() + "\n\n";
            sink.write(error_msg.c_str(), error_msg.size());
            sink.done();
            return;
        }

        InhibitGuard inhibit_guard(suspend_inhibitor_.get(), config_->inhibit_suspend());

        try {
            streaming_func(server);
            const bool watchdog_reset = server->was_watchdog_triggered();

            if (watchdog_reset) {
                restart_options = server->get_recipe_options();
                restart_model_name = server->get_model_name();
            }

            server->release_inference();

            // Do not replay a streaming response after bytes may have reached the
            // client. Reload immediately so the next request does not see a
            // stale tombstone, then return the stream outcome as-is.
            if (watchdog_reset) {
                if (restart_model_name.empty()) {
                    restart_model_name = requested_model;
                }
                reload_model_after_watchdog_reset(restart_model_name, restart_options);
            }
            return;
        } catch (const BackendStreamRetryableReset& e) {
            restart_options = server->get_recipe_options();
            restart_model_name = server->get_model_name();
            server->release_inference();

            if (restart_model_name.empty()) {
                restart_model_name = requested_model;
            }
            if (attempt == 0 && reload_model_after_watchdog_reset(restart_model_name, restart_options)) {
                continue;
            }

            if (span) {
                span->end_with_error(e.what());
            }

            json error = ErrorResponse::create(
                std::string("Backend for model '") + requested_model +
                    "' crashed before streaming started and could not be reloaded: " + e.what(),
                ErrorType::BACKEND_ERROR,
                {{"code", "backend_watchdog_reset"}, {"retryable", true}}
            );
            std::string error_msg = "data: " + error.dump() + "\n\n";
            sink.write(error_msg.c_str(), error_msg.size());
            sink.done();
            return;
        } catch (...) {
            server->release_inference();
            throw;
        }
    }
}

json Router::chat_completion(const json& request, std::atomic<bool>* cancel) {
    std::string requested_model = request.value("model", "");
    std::shared_ptr<telemetry::InferenceSpan> span = telemetry::TelemetryTracker::start_span("LLM", "chat.completions", requested_model, request);

    struct RequestCancelScope {
        WrappedServer* server = nullptr;
        ~RequestCancelScope() {
            if (server) server->set_request_cancel_flag(nullptr);
        }
    } cancel_scope;

    try {
        WrappedServer* active_server = nullptr;
        json response = execute_inference(request, [&](WrappedServer* server) {
            active_server = server;
            if (cancel) {
                server->set_request_cancel_flag(cancel);
                cancel_scope.server = server;
            }
            ModelTelemetryIdentity identity = get_telemetry_identity(server);
            if (span) {
                span->set_attribute("llm.backend", identity.recipe);
                span->set_attribute("llm.device_type", identity.device);
                span->set_attribute("llm.checkpoint", identity.checkpoint);
                span->set_attribute("llm.recipe", identity.recipe);
                if (request.contains("temperature")) span->set_attribute("llm.config.temperature", request["temperature"]);
                if (request.contains("top_p")) span->set_attribute("llm.config.top_p", request["top_p"]);
                if (request.contains("max_tokens")) span->set_attribute("llm.config.max_tokens", request["max_tokens"]);
                if (request.contains("max_completion_tokens")) span->set_attribute("llm.config.max_completion_tokens", request["max_completion_tokens"]);
            }
            return server->chat_completion(request);
        });

        if (span) {
            if (response.contains("error")) {
                std::string error_msg = "Request failed";
                if (response["error"].contains("message") && response["error"]["message"].is_string()) {
                    error_msg = response["error"]["message"].get<std::string>();
                }
                span->end_with_error(error_msg);
            } else {
                nlohmann::json usage_payload = nlohmann::json::object();
                std::string text_output = "";
                if (response.contains("usage") && response["usage"].is_object()) {
                    auto usage = response["usage"];
                    if (usage.contains("prompt_tokens")) {
                        usage_payload["prompt_tokens"] = usage["prompt_tokens"].get<int>();
                    } else if (usage.contains("input_tokens")) {
                        usage_payload["prompt_tokens"] = usage["input_tokens"].get<int>();
                    }
                    if (usage.contains("completion_tokens")) {
                        usage_payload["completion_tokens"] = usage["completion_tokens"].get<int>();
                    } else if (usage.contains("output_tokens")) {
                        usage_payload["completion_tokens"] = usage["output_tokens"].get<int>();
                    }
                }
                if (response.contains("timings")) {
                    auto timings = response["timings"];
                    if (timings.contains("prompt_n")) usage_payload["prompt_tokens"] = timings["prompt_n"].get<int>();
                    if (timings.contains("predicted_n")) usage_payload["completion_tokens"] = timings["predicted_n"].get<int>();

                    if (timings.contains("prompt_ms") && timings.contains("prompt_n")) {
                        double prompt_ms = timings["prompt_ms"].get<double>();
                        if (prompt_ms > 0) {
                            span->set_attribute("llm.performance.time_to_first_token", prompt_ms / 1000.0);
                        }
                    }
                    if (timings.contains("predicted_ms") && timings.contains("predicted_n")) {
                        double predicted_ms = timings["predicted_ms"].get<double>();
                        int predicted_n = timings["predicted_n"].get<int>();
                        if (predicted_ms > 0 && predicted_n > 0) {
                            span->set_attribute("llm.performance.tokens_per_second", (predicted_n / (predicted_ms / 1000.0)));
                        }
                    }
                }

                if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                    auto choice = response["choices"][0];
                    std::string reasoning_output = "";
                    if (choice.contains("message")) {
                        auto msg = choice["message"];
                        if (msg.contains("reasoning_content") && msg["reasoning_content"].is_string()) {
                            reasoning_output = msg["reasoning_content"].get<std::string>();
                        } else if (msg.contains("thinking") && msg["thinking"].is_string()) {
                            reasoning_output = msg["thinking"].get<std::string>();
                        }
                        if (msg.contains("content") && msg["content"].is_string()) {
                            text_output = msg["content"].get<std::string>();
                        }
                    }
                    if (!reasoning_output.empty()) {
                        text_output = "<think>\n" + reasoning_output + "\n</think>\n" + text_output;
                    }
                }

                std::string url;
                std::function<std::map<std::string, nlohmann::json>(const std::string&)> parser;
                if (active_server) {
                    url = active_server->get_additional_telemetry_url();
                    parser = active_server->get_additional_telemetry_parser();
                }
                telemetry::end_llm_span_async(span, url, parser, usage_payload, text_output);
            }
        }
        return response;
    } catch (const std::exception& e) {
        if (span) span->end_with_error(e.what());
        throw;
    }
}

json Router::completion(const json& request) {
    std::string requested_model = request.value("model", "");
    std::shared_ptr<telemetry::InferenceSpan> span = telemetry::TelemetryTracker::start_span("LLM", "completions", requested_model, request);

    try {
        WrappedServer* active_server = nullptr;
        json response = execute_inference(request, [&](WrappedServer* server) {
            active_server = server;
            ModelTelemetryIdentity identity = get_telemetry_identity(server);
            if (span) {
                span->set_attribute("llm.backend", identity.recipe);
                span->set_attribute("llm.device_type", identity.device);
                span->set_attribute("llm.checkpoint", identity.checkpoint);
                span->set_attribute("llm.recipe", identity.recipe);
                if (request.contains("temperature")) span->set_attribute("llm.config.temperature", request["temperature"]);
                if (request.contains("top_p")) span->set_attribute("llm.config.top_p", request["top_p"]);
                if (request.contains("max_tokens")) span->set_attribute("llm.config.max_tokens", request["max_tokens"]);
            }
            return server->completion(request);
        });

        if (span) {
            if (response.contains("error")) {
                std::string error_msg = "Request failed";
                if (response["error"].contains("message") && response["error"]["message"].is_string()) {
                    error_msg = response["error"]["message"].get<std::string>();
                }
                span->end_with_error(error_msg);
            } else {
                nlohmann::json usage_payload = nlohmann::json::object();
                std::string text_output = "";
                if (response.contains("usage") && response["usage"].is_object()) {
                    auto usage = response["usage"];
                    if (usage.contains("prompt_tokens")) {
                        usage_payload["prompt_tokens"] = usage["prompt_tokens"].get<int>();
                    } else if (usage.contains("input_tokens")) {
                        usage_payload["prompt_tokens"] = usage["input_tokens"].get<int>();
                    }
                    if (usage.contains("completion_tokens")) {
                        usage_payload["completion_tokens"] = usage["completion_tokens"].get<int>();
                    } else if (usage.contains("output_tokens")) {
                        usage_payload["completion_tokens"] = usage["output_tokens"].get<int>();
                    }
                }

                if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                    auto choice = response["choices"][0];
                    if (choice.contains("text") && choice["text"].is_string()) {
                        text_output = choice["text"].get<std::string>();
                    }
                }

                std::string url;
                std::function<std::map<std::string, nlohmann::json>(const std::string&)> parser;
                if (active_server) {
                    url = active_server->get_additional_telemetry_url();
                    parser = active_server->get_additional_telemetry_parser();
                }
                telemetry::end_llm_span_async(span, url, parser, usage_payload, text_output);
            }
        }
        return response;
    } catch (const std::exception& e) {
        if (span) span->end_with_error(e.what());
        throw;
    }
}

json Router::embeddings(const json& request) {
    std::string requested_model = request.value("model", "");
    std::shared_ptr<telemetry::InferenceSpan> span = telemetry::TelemetryTracker::start_span("EMBEDDING", "embeddings", requested_model, request);

    try {
        json response = execute_inference(request, [&](WrappedServer* server) {
            ModelTelemetryIdentity identity = get_telemetry_identity(server);
            if (span) {
                span->set_attribute("embedding.backend", identity.recipe);
                span->set_attribute("embedding.device_type", identity.device);
                span->set_attribute("embedding.checkpoint", identity.checkpoint);
                span->set_attribute("embedding.recipe", identity.recipe);
            }
            auto embeddings_server = dynamic_cast<IEmbeddingsServer*>(server);
            if (!embeddings_server) {
                return ErrorResponse::from_exception(
                    UnsupportedOperationException("Embeddings", device_type_to_string(server->get_device_type()))
                );
            }
            return embeddings_server->embeddings(request);
        });

        if (span) {
            if (response.contains("error")) {
                std::string error_msg = "Request failed";
                if (response["error"].contains("message") && response["error"]["message"].is_string()) {
                    error_msg = response["error"]["message"].get<std::string>();
                }
                span->end_with_error(error_msg);
            } else {
                nlohmann::json usage_payload = nlohmann::json::object();
                if (response.contains("usage") && response["usage"].is_object()) {
                    auto usage = response["usage"];
                    if (usage.contains("prompt_tokens")) {
                        usage_payload["prompt_tokens"] = usage["prompt_tokens"].get<int>();
                    } else if (usage.contains("input_tokens")) {
                        usage_payload["prompt_tokens"] = usage["input_tokens"].get<int>();
                    }
                    if (usage.contains("total_tokens")) usage_payload["total_tokens"] = usage["total_tokens"].get<int>();
                }
                std::string output_dump = "";
                if (response.contains("data") && response["data"].is_array()) {
                    output_dump = "Embeddings data with " + std::to_string(response["data"].size()) + " vectors.";
                } else {
                    output_dump = response.dump();
                }
                span->end_with_success(usage_payload, output_dump);
            }
        }
        return response;
    } catch (const std::exception& e) {
        if (span) span->end_with_error(e.what());
        throw;
    }
}

json Router::reranking(const json& request) {
    std::string requested_model = request.value("model", "");
    std::shared_ptr<telemetry::InferenceSpan> span = telemetry::TelemetryTracker::start_span("RERANKER", "reranking", requested_model, request);

    try {
        json response = execute_inference(request, [&](WrappedServer* server) {
            ModelTelemetryIdentity identity = get_telemetry_identity(server);
            if (span) {
                span->set_attribute("reranker.backend", identity.recipe);
                span->set_attribute("reranker.device_type", identity.device);
                span->set_attribute("reranker.checkpoint", identity.checkpoint);
                span->set_attribute("reranker.recipe", identity.recipe);
            }
            auto reranking_server = dynamic_cast<IRerankingServer*>(server);
            if (!reranking_server) {
                return ErrorResponse::from_exception(
                    UnsupportedOperationException("Reranking", device_type_to_string(server->get_device_type()))
                );
            }
            return reranking_server->reranking(request);
        });

        if (span) {
            if (response.contains("error")) {
                std::string error_msg = "Request failed";
                if (response["error"].contains("message") && response["error"]["message"].is_string()) {
                    error_msg = response["error"]["message"].get<std::string>();
                }
                span->end_with_error(error_msg);
            } else {
                nlohmann::json usage_payload = nlohmann::json::object();
                if (response.contains("usage") && response["usage"].is_object()) {
                    auto usage = response["usage"];
                    if (usage.contains("prompt_tokens")) {
                        usage_payload["prompt_tokens"] = usage["prompt_tokens"].get<int>();
                    } else if (usage.contains("input_tokens")) {
                        usage_payload["prompt_tokens"] = usage["input_tokens"].get<int>();
                    }
                    if (usage.contains("total_tokens")) usage_payload["total_tokens"] = usage["total_tokens"].get<int>();
                }
                span->end_with_success(usage_payload, response.dump());
            }
        }
        return response;
    } catch (const std::exception& e) {
        if (span) span->end_with_error(e.what());
        throw;
    }
}

json Router::classify(const json& request) {
    std::string requested_model = request.value("model", "");
    std::shared_ptr<telemetry::InferenceSpan> span = telemetry::TelemetryTracker::start_span("CLASSIFIER", "classify", requested_model, request);

    try {
        json response = execute_inference(request, [&](WrappedServer* server) {
            ModelTelemetryIdentity identity = get_telemetry_identity(server);
            if (span) {
                span->set_attribute("classifier.backend", identity.recipe);
                span->set_attribute("classifier.device_type", identity.device);
                span->set_attribute("classifier.checkpoint", identity.checkpoint);
                span->set_attribute("classifier.recipe", identity.recipe);
            }
            auto classification_server = dynamic_cast<IClassificationServer*>(server);
            if (!classification_server) {
                return ErrorResponse::from_exception(
                    UnsupportedOperationException("Classification", device_type_to_string(server->get_device_type()))
                );
            }
            return classification_server->classify(request);
        });

        if (span) {
            if (response.contains("error")) {
                std::string error_msg = "Request failed";
                if (response["error"].contains("message") && response["error"]["message"].is_string()) {
                    error_msg = response["error"]["message"].get<std::string>();
                }
                span->end_with_error(error_msg);
            } else {
                // Label scores classify user content; keep them out of telemetry.
                span->end_with_success(nlohmann::json::object(), "");
            }
        }
        return response;
    } catch (const std::exception& e) {
        if (span) span->end_with_error(e.what());
        throw;
    }
}

json Router::get_slots() {
    WrappedServer* server = nullptr;
    ISlotsServer* slots_server = nullptr;

    {
        std::unique_lock<std::mutex> lock(load_mutex_);
        wait_for_slot_clearance(lock);
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
        if (!server->acquire_for_inference()) {
            return ErrorResponse::from_exception(ModelNotLoadedException("No models loaded"));
        }
        server->update_access_time();
    } // Lock released here

    // Execute without holding lock (but busy flag prevents eviction)
    try {
        auto response = slots_server->get_slots();
        server->release_inference();
        return response;
    } catch (...) {
        server->release_inference();
        throw;
    }
}

json Router::slots_action(int slot_id, const std::string& action, const json& request_body) {
    WrappedServer* server = nullptr;
    ISlotsServer* slots_server = nullptr;

    {
        std::unique_lock<std::mutex> lock(load_mutex_);
        wait_for_slot_clearance(lock);
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
        if (!server->acquire_for_inference()) {
            return ErrorResponse::from_exception(ModelNotLoadedException("No models loaded"));
        }
        server->update_access_time();
    } // Lock released here

    // Execute without holding lock (but busy flag prevents eviction)
    try {
        auto response = slots_server->slots_action(slot_id, action, request_body);
        server->release_inference();
        return response;
    } catch (...) {
        server->release_inference();
        throw;
    }
}

json Router::tokenize(const json& request_body) {
    WrappedServer* server = nullptr;
    ITokenizerServer* tokenizer_server = nullptr;

    {
        std::unique_lock<std::mutex> lock(load_mutex_);
        wait_for_slot_clearance(lock);
        server = get_most_recent_server();
        if (!server) {
            return ErrorResponse::from_exception(
                ModelNotLoadedException("No models loaded")
            );
        }

        // Check if server supports tokenize capability
        tokenizer_server = dynamic_cast<ITokenizerServer*>(server);
        if (!tokenizer_server) {
            return ErrorResponse::from_exception(
                UnsupportedOperationException("Tokenization", device_type_to_string(server->get_device_type()))
            );
        }

        // Mark as busy and update access time
        if (!server->acquire_for_inference()) {
            return ErrorResponse::from_exception(ModelNotLoadedException("No models loaded"));
        }
        server->update_access_time();
    } // Lock released here

    // Execute without holding lock (but busy flag prevents eviction)
    try {
        auto response = tokenizer_server->tokenize(request_body);
        server->release_inference();
        return response;
    } catch (...) {
        server->release_inference();
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
        auto transcription_server = dynamic_cast<ITranscriptionServer*>(server);
        if (!transcription_server) {
            return ErrorResponse::from_exception(
                UnsupportedOperationException("Audio transcription", device_type_to_string(server->get_device_type()))
            );
        }
        return transcription_server->audio_transcriptions(request);
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

std::vector<std::string> Router::audio_speech_supported_formats(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(load_mutex_);
    auto tts_server = dynamic_cast<ITextToSpeechServer*>(
        find_server_by_model_name(resolve_model_name(model_name)));
    return tts_server ? tts_server->supported_audio_formats() : std::vector<std::string>{};
}

std::vector<std::string> Router::audio_speech_supported_streaming_formats(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(load_mutex_);
    auto tts_server = dynamic_cast<ITextToSpeechServer*>(
        find_server_by_model_name(resolve_model_name(model_name)));
    return tts_server ? tts_server->supported_streaming_audio_formats() : std::vector<std::string>{};
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

void Router::audio_generations(const json& request, httplib::DataSink& sink) {
    execute_streaming(request.dump(), sink, [&](WrappedServer* server) {
        auto audio_server = dynamic_cast<IAudioGenerationServer*>(server);
        if (!audio_server) {
            throw UnsupportedOperationException("Audio generation", device_type_to_string(server->get_device_type()));
        }
        audio_server->audio_generations(request, sink);
    });
}

std::vector<std::string> Router::audio_generation_supported_formats(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(load_mutex_);
    auto audio_server = dynamic_cast<IAudioGenerationServer*>(
        find_server_by_model_name(resolve_model_name(model_name)));
    return audio_server ? audio_server->supported_audio_formats() : std::vector<std::string>{};
}

void Router::model_3d_generations(const json& request, httplib::DataSink& sink) {
    execute_streaming(request.dump(), sink, [&](WrappedServer* server) {
        auto model_server = dynamic_cast<IModel3DServer*>(server);
        if (!model_server) {
            throw UnsupportedOperationException("3D generation", device_type_to_string(server->get_device_type()));
        }
        model_server->model_3d_generations(request, sink);
    });
}

json Router::get_stats() const {
    std::lock_guard<std::mutex> lock(telemetry_mutex_);
    json stats = aggregate_telemetry_.to_json();
    stats["routing_decisions_total"] = routing_decisions_total_;
    stats["routing_switches_total"] = routing_switches_total_;
    return stats;
}

json Router::get_metrics_snapshot() const {
    json result;
    result["loaded_models"] = json::array();
    result["model_metrics"] = json::array();
    result["totals"] = {
        {"requests", 0},
        {"input_tokens", 0},
        {"output_tokens", 0},
        {"prompt_tokens", 0},
        {"cache_tokens", 0},
        {"routing_decisions", 0},
        {"routing_switches", 0}
    };

    std::map<std::string, ModelTelemetryIdentity> loaded_identities;

    {
        std::lock_guard<std::mutex> lock(load_mutex_);
        for (const auto& server : loaded_servers_) {
            ModelTelemetryIdentity identity = get_telemetry_identity(server.get());
            loaded_identities[identity.key()] = identity;

            json model_info;
            model_info["model_name"] = model_manager_->get_public_model_name(identity.model_name);
            model_info["checkpoint"] = identity.checkpoint;
            model_info["type"] = identity.type;
            model_info["device"] = identity.device;
            model_info["backend_url"] = server->get_address();
            model_info["pid"] = server->get_process_id();
            model_info["recipe"] = identity.recipe;
            result["loaded_models"].push_back(model_info);
        }
    }

    {
        std::lock_guard<std::mutex> lock(telemetry_mutex_);
        for (const auto& item : telemetry_by_model_) {
            const auto& record = item.second;
            json model_info;
            model_info["model_name"] = model_manager_->get_public_model_name(record.identity.model_name);
            model_info["checkpoint"] = record.identity.checkpoint;
            model_info["type"] = record.identity.type;
            model_info["device"] = record.identity.device;
            model_info["recipe"] = record.identity.recipe;
            model_info["loaded"] = loaded_identities.find(item.first) != loaded_identities.end();
            model_info["telemetry"] = record.telemetry.to_json();
            result["model_metrics"].push_back(model_info);
        }

        for (const auto& item : loaded_identities) {
            if (telemetry_by_model_.find(item.first) != telemetry_by_model_.end()) {
                continue;
            }
            const auto& identity = item.second;
            json model_info;
            model_info["model_name"] = model_manager_->get_public_model_name(identity.model_name);
            model_info["checkpoint"] = identity.checkpoint;
            model_info["type"] = identity.type;
            model_info["device"] = identity.device;
            model_info["recipe"] = identity.recipe;
            model_info["loaded"] = true;
            model_info["telemetry"] = Telemetry().to_json();
            result["model_metrics"].push_back(model_info);
        }

        result["totals"]["requests"] = aggregate_telemetry_.request_count_total;
        result["totals"]["input_tokens"] = aggregate_telemetry_.input_tokens_total;
        result["totals"]["output_tokens"] = aggregate_telemetry_.output_tokens_total;
        result["totals"]["prompt_tokens"] = aggregate_telemetry_.prompt_tokens_total;
        result["totals"]["cache_tokens"] = aggregate_telemetry_.cache_tokens_total;
        result["totals"]["routing_decisions"] = routing_decisions_total_;
        result["totals"]["routing_switches"] = routing_switches_total_;
    }

    return result;
}

ModelTelemetryIdentity Router::get_telemetry_identity(WrappedServer* server) const {
    if (!server) {
        return {};
    }

    RecipeOptions recipe_options = server->get_recipe_options();
    return {
        server->get_model_name(),
        server->get_checkpoint(),
        model_type_to_string(server->get_model_type()),
        device_type_to_string(server->get_device_type()),
        recipe_options.get_recipe()
    };
}

void Router::record_request_telemetry_for_model(const ModelTelemetryIdentity& identity,
                                                const StreamingProxy::TelemetryData& telemetry) {
    if (identity.model_name.empty()) {
        return;
    }

    // One request = one atomic update under a single lock hold, so concurrent
    // requests can interleave whole updates but never mix fields of two
    // requests (e.g. one request's cache reset landing on another's value).
    const int prompt_tokens = telemetry.prompt_tokens >= 0
        ? telemetry.prompt_tokens
        : (telemetry.input_tokens > 0 ? telemetry.input_tokens : -1);

    std::lock_guard<std::mutex> lock(telemetry_mutex_);
    ModelTelemetryRecord& record = telemetry_by_model_[identity.key()];
    record.identity = identity;

    for (Telemetry* t : {&record.telemetry, &aggregate_telemetry_}) {
        t->input_tokens = telemetry.input_tokens;
        t->output_tokens = telemetry.output_tokens;
        t->time_to_first_token = telemetry.time_to_first_token;
        t->tokens_per_second = telemetry.tokens_per_second;
        t->cache_tokens = telemetry.cache_tokens >= 0 ? telemetry.cache_tokens : -1;
        t->request_count_total++;
        if (prompt_tokens >= 0) {
            t->prompt_tokens = prompt_tokens;
        }
        if (telemetry.input_tokens > 0) {
            t->input_tokens_total += static_cast<uint64_t>(telemetry.input_tokens);
        }
        if (telemetry.output_tokens > 0) {
            t->output_tokens_total += static_cast<uint64_t>(telemetry.output_tokens);
        }
        if (prompt_tokens > 0) {
            t->prompt_tokens_total += static_cast<uint64_t>(prompt_tokens);
        }
        if (telemetry.cache_tokens > 0) {
            t->cache_tokens_total += static_cast<uint64_t>(telemetry.cache_tokens);
        }
    }
}

void Router::note_route_decision(uint64_t conversation_fingerprint, const std::string& route_to) {
    static constexpr size_t kRouteFingerprintCapacity = 1024;

    std::lock_guard<std::mutex> lock(telemetry_mutex_);
    routing_decisions_total_++;

    auto it = route_last_target_.find(conversation_fingerprint);
    if (it != route_last_target_.end()) {
        if (it->second.first != route_to) {
            routing_switches_total_++;
            it->second.first = route_to;
        }
        route_fingerprint_lru_.splice(route_fingerprint_lru_.begin(),
                                      route_fingerprint_lru_, it->second.second);
        return;
    }

    route_fingerprint_lru_.push_front(conversation_fingerprint);
    route_last_target_[conversation_fingerprint] = {route_to, route_fingerprint_lru_.begin()};
    if (route_last_target_.size() > kRouteFingerprintCapacity) {
        route_last_target_.erase(route_fingerprint_lru_.back());
        route_fingerprint_lru_.pop_back();
    }
}

void Router::update_request_telemetry(const std::string& model_name,
                                      const StreamingProxy::TelemetryData& telemetry) {
    ModelTelemetryIdentity identity;
    {
        std::lock_guard<std::mutex> lock(load_mutex_);
        WrappedServer* server = model_name.empty()
            ? get_most_recent_server()
            : find_server_by_model_name(resolve_model_name(model_name));
        identity = get_telemetry_identity(server);
    }
    record_request_telemetry_for_model(identity, telemetry);
}

void Router::chat_completion_stream(const std::string& request_body, httplib::DataSink& sink) {
    json request_json;
    try {
        request_json = json::parse(request_body);
    } catch (...) {}
    std::string requested_model = request_json.value("model", "");

    std::shared_ptr<telemetry::InferenceSpan> span = telemetry::TelemetryTracker::start_span("LLM", "chat.completions", requested_model, request_json);

    bool hide_outputs = false;
    bool hide_thinking = false;
    if (auto* config = RuntimeConfig::global()) {
        hide_outputs = config->telemetry_hide_outputs();
        hide_thinking = config->telemetry_hide_thinking();
    }

    auto accumulated_text = std::make_shared<std::string>();
    auto accumulated_reasoning = std::make_shared<std::string>();
    auto line_buffer = std::make_shared<std::string>();

    httplib::DataSink telemetry_sink;
    telemetry_sink.write = [accumulated_text, accumulated_reasoning, line_buffer, &sink, hide_outputs, hide_thinking](const char* data, size_t len) -> bool {
        bool success = false;
        if (sink.write) {
            success = sink.write(data, len);
        }
        line_buffer->append(data, len);
        StreamingProxy::process_sse_lines(*line_buffer, [accumulated_text, accumulated_reasoning, hide_outputs, hide_thinking](const std::string& line) {
            if (line.rfind("data: ", 0) == 0) {
                std::string json_str = line.substr(6);
                if (json_str.find("[DONE]") == std::string::npos) {
                    try {
                        auto parsed = json::parse(json_str);
                        if (parsed.contains("choices") && parsed["choices"].is_array() && !parsed["choices"].empty()) {
                            auto delta = parsed["choices"][0]["delta"];
                            if (!hide_thinking) {
                                if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
                                    *accumulated_reasoning += delta["reasoning_content"].get<std::string>();
                                } else if (delta.contains("thinking") && delta["thinking"].is_string()) {
                                    *accumulated_reasoning += delta["thinking"].get<std::string>();
                                }
                            }
                            if (!hide_outputs) {
                                if (delta.contains("content") && delta["content"].is_string()) {
                                    *accumulated_text += delta["content"].get<std::string>();
                                }
                            }
                        }
                    } catch (...) {}
                }
            }
        });
        return success;
    };

    telemetry_sink.is_writable = [&sink]() -> bool {
        return sink.is_writable ? sink.is_writable() : true;
    };

    telemetry_sink.done = [&sink]() {
        if (sink.done) {
            sink.done();
        }
    };

    telemetry_sink.done_with_trailer = [&sink](const httplib::Headers& trailer) {
        if (sink.done_with_trailer) {
            sink.done_with_trailer(trailer);
        } else if (sink.done) {
            sink.done();
        }
    };

    try {
        execute_streaming(request_body, telemetry_sink, [&](WrappedServer* server) {
            ModelTelemetryIdentity identity = get_telemetry_identity(server);

            if (span) {
                span->set_attribute("llm.backend", identity.recipe);
                span->set_attribute("llm.device_type", identity.device);
                span->set_attribute("llm.checkpoint", identity.checkpoint);
                span->set_attribute("llm.recipe", identity.recipe);
                if (request_json.contains("temperature")) span->set_attribute("llm.config.temperature", request_json["temperature"]);
                if (request_json.contains("top_p")) span->set_attribute("llm.config.top_p", request_json["top_p"]);
                if (request_json.contains("max_tokens")) span->set_attribute("llm.config.max_tokens", request_json["max_tokens"]);
                if (request_json.contains("max_completion_tokens")) span->set_attribute("llm.config.max_completion_tokens", request_json["max_completion_tokens"]);
            }

            server->forward_streaming_request("/v1/chat/completions", request_body, telemetry_sink, true, 0,
                [this, identity, span, accumulated_text, accumulated_reasoning, server](
                    const StreamingProxy::TelemetryData& telemetry) {
                    if (!telemetry.error_message.empty()) {
                        if (span) {
                            span->end_with_error(telemetry.error_message);
                        }
                        return;
                    }
                    record_request_telemetry_for_model(identity, telemetry);

                    if (span) {
                        nlohmann::json usage_payload = {
                            {"prompt_tokens", telemetry.input_tokens},
                            {"completion_tokens", telemetry.output_tokens}
                        };
                        span->set_attribute("llm.performance.time_to_first_token", telemetry.time_to_first_token);
                        span->set_attribute("llm.performance.tokens_per_second", telemetry.tokens_per_second);
                        std::string final_output = *accumulated_text;
                        if (!accumulated_reasoning->empty()) {
                            final_output = "<think>\n" + *accumulated_reasoning + "\n</think>\n" + final_output;
                        }

                        std::string url;
                        std::function<std::map<std::string, nlohmann::json>(const std::string&)> parser;
                        if (server) {
                            url = server->get_additional_telemetry_url();
                            parser = server->get_additional_telemetry_parser();
                        }
                        telemetry::end_llm_span_async(span, url, parser, usage_payload, final_output);
                    }
                });
        }, span);
    } catch (const std::exception& e) {
        if (span) span->end_with_error(e.what());
        throw;
    } catch (...) {
        if (span) span->end_with_error("Unknown error during streaming");
        throw;
    }
}

void Router::completion_stream(const std::string& request_body, httplib::DataSink& sink) {
    json request_json;
    try {
        request_json = json::parse(request_body);
    } catch (...) {}
    std::string requested_model = request_json.value("model", "");

    std::shared_ptr<telemetry::InferenceSpan> span = telemetry::TelemetryTracker::start_span("LLM", "completions", requested_model, request_json);

    bool hide_outputs = false;
    if (auto* config = RuntimeConfig::global()) {
        hide_outputs = config->telemetry_hide_outputs();
    }

    auto accumulated_text = std::make_shared<std::string>();
    auto line_buffer = std::make_shared<std::string>();

    httplib::DataSink telemetry_sink;
    telemetry_sink.write = [accumulated_text, line_buffer, &sink, hide_outputs](const char* data, size_t len) -> bool {
        bool success = false;
        if (sink.write) {
            success = sink.write(data, len);
        }
        line_buffer->append(data, len);
        StreamingProxy::process_sse_lines(*line_buffer, [accumulated_text, hide_outputs](const std::string& line) {
            if (line.rfind("data: ", 0) == 0) {
                std::string json_str = line.substr(6);
                if (json_str.find("[DONE]") == std::string::npos) {
                    try {
                        auto parsed = json::parse(json_str);
                        if (parsed.contains("choices") && parsed["choices"].is_array() && !parsed["choices"].empty()) {
                            auto choice = parsed["choices"][0];
                            if (!hide_outputs) {
                                if (choice.contains("text") && choice["text"].is_string()) {
                                    *accumulated_text += choice["text"].get<std::string>();
                                }
                            }
                        }
                    } catch (...) {}
                }
            }
        });
        return success;
    };

    telemetry_sink.is_writable = [&sink]() -> bool {
        return sink.is_writable ? sink.is_writable() : true;
    };

    telemetry_sink.done = [&sink]() {
        if (sink.done) {
            sink.done();
        }
    };

    telemetry_sink.done_with_trailer = [&sink](const httplib::Headers& trailer) {
        if (sink.done_with_trailer) {
            sink.done_with_trailer(trailer);
        } else if (sink.done) {
            sink.done();
        }
    };

    try {
        execute_streaming(request_body, telemetry_sink, [&](WrappedServer* server) {
            ModelTelemetryIdentity identity = get_telemetry_identity(server);

            if (span) {
                span->set_attribute("llm.backend", identity.recipe);
                span->set_attribute("llm.device_type", identity.device);
                span->set_attribute("llm.checkpoint", identity.checkpoint);
                span->set_attribute("llm.recipe", identity.recipe);
                if (request_json.contains("temperature")) span->set_attribute("llm.config.temperature", request_json["temperature"]);
                if (request_json.contains("top_p")) span->set_attribute("llm.config.top_p", request_json["top_p"]);
                if (request_json.contains("max_tokens")) span->set_attribute("llm.config.max_tokens", request_json["max_tokens"]);
            }

            server->forward_streaming_request("/v1/completions", request_body, telemetry_sink, true, 0,
                [this, identity, span, accumulated_text, server](
                    const StreamingProxy::TelemetryData& telemetry) {
                    if (!telemetry.error_message.empty()) {
                        if (span) {
                            span->end_with_error(telemetry.error_message);
                        }
                        return;
                    }
                    record_request_telemetry_for_model(identity, telemetry);

                    if (span) {
                        nlohmann::json usage_payload = {
                            {"prompt_tokens", telemetry.input_tokens},
                            {"completion_tokens", telemetry.output_tokens}
                        };
                        span->set_attribute("llm.performance.time_to_first_token", telemetry.time_to_first_token);
                        span->set_attribute("llm.performance.tokens_per_second", telemetry.tokens_per_second);

                        std::string url;
                        std::function<std::map<std::string, nlohmann::json>(const std::string&)> parser;
                        if (server) {
                            url = server->get_additional_telemetry_url();
                            parser = server->get_additional_telemetry_parser();
                        }
                        telemetry::end_llm_span_async(span, url, parser, usage_payload, *accumulated_text);
                    }
                });
        }, span);
    } catch (const std::exception& e) {
        if (span) span->end_with_error(e.what());
        throw;
    } catch (...) {
        if (span) span->end_with_error("Unknown error during streaming");
        throw;
    }
}

void Router::responses_stream(const std::string& request_body, httplib::DataSink& sink) {
    json request_json;
    try {
        request_json = json::parse(request_body);
    } catch (...) {}
    std::string requested_model = request_json.value("model", "");

    std::shared_ptr<telemetry::InferenceSpan> span = telemetry::TelemetryTracker::start_span("LLM", "responses", requested_model, request_json);

    bool hide_outputs = false;
    if (auto* config = RuntimeConfig::global()) {
        hide_outputs = config->telemetry_hide_outputs();
    }

    auto accumulated_text = std::make_shared<std::string>();
    auto line_buffer = std::make_shared<std::string>();

    httplib::DataSink telemetry_sink;
    telemetry_sink.write = [accumulated_text, line_buffer, &sink, hide_outputs](const char* data, size_t len) -> bool {
        bool success = false;
        if (sink.write) {
            success = sink.write(data, len);
        }
        line_buffer->append(data, len);
        StreamingProxy::process_sse_lines(*line_buffer, [accumulated_text, hide_outputs](const std::string& line) {
            if (line.rfind("data: ", 0) == 0) {
                std::string json_str = line.substr(6);
                if (json_str.find("[DONE]") == std::string::npos) {
                    try {
                        auto parsed = json::parse(json_str);
                        if (!hide_outputs) {
                            StreamingProxy::accumulate_responses_delta(parsed, *accumulated_text);
                        }
                    } catch (...) {}
                }
            }
        });
        return success;
    };

    telemetry_sink.is_writable = [&sink]() -> bool {
        return sink.is_writable ? sink.is_writable() : true;
    };

    telemetry_sink.done = [&sink]() {
        if (sink.done) {
            sink.done();
        }
    };

    telemetry_sink.done_with_trailer = [&sink](const httplib::Headers& trailer) {
        if (sink.done_with_trailer) {
            sink.done_with_trailer(trailer);
        } else if (sink.done) {
            sink.done();
        }
    };

    try {
        execute_streaming(request_body, telemetry_sink, [&](WrappedServer* server) {
            ModelTelemetryIdentity identity = get_telemetry_identity(server);

            if (span) {
                span->set_attribute("llm.backend", identity.recipe);
                span->set_attribute("llm.device_type", identity.device);
                span->set_attribute("llm.checkpoint", identity.checkpoint);
                span->set_attribute("llm.recipe", identity.recipe);
                if (request_json.contains("temperature")) span->set_attribute("llm.config.temperature", request_json["temperature"]);
                if (request_json.contains("top_p")) span->set_attribute("llm.config.top_p", request_json["top_p"]);
                if (request_json.contains("max_tokens")) span->set_attribute("llm.config.max_tokens", request_json["max_tokens"]);
            }

            server->forward_streaming_request("/v1/responses", request_body, telemetry_sink, true, 0,
                [this, identity, span, accumulated_text, server](
                    const StreamingProxy::TelemetryData& telemetry) {
                    if (!telemetry.error_message.empty()) {
                        if (span) {
                            span->end_with_error(telemetry.error_message);
                        }
                        return;
                    }
                    record_request_telemetry_for_model(identity, telemetry);

                    if (span) {
                        nlohmann::json usage_payload = {
                            {"prompt_tokens", telemetry.input_tokens},
                            {"completion_tokens", telemetry.output_tokens}
                        };
                        span->set_attribute("llm.performance.time_to_first_token", telemetry.time_to_first_token);
                        span->set_attribute("llm.performance.tokens_per_second", telemetry.tokens_per_second);

                        std::string url;
                        std::function<std::map<std::string, nlohmann::json>(const std::string&)> parser;
                        if (server) {
                            url = server->get_additional_telemetry_url();
                            parser = server->get_additional_telemetry_parser();
                        }
                        telemetry::end_llm_span_async(span, url, parser, usage_payload, *accumulated_text);
                    }
                });
        }, span);
    } catch (const std::exception& e) {
        if (span) span->end_with_error(e.what());
        throw;
    } catch (...) {
        if (span) span->end_with_error("Unknown error during streaming");
        throw;
    }
}

int Router::count_pinned_servers_in_pool(
    ModelType type,
    ResidencyClass residency_class) const {
    int count = 0;
    for (const auto& server : loaded_servers_) {
        if (is_unmetered_recipe(server->get_recipe_options().get_recipe())) {
            continue;
        }
        if (server->is_backend_alive() &&
            server->get_model_type() == type &&
            server->get_residency_class() == residency_class &&
            server->is_pinned()) {
            count++;
        }
    }
    return count;
}

json Router::get_pinned_model_counts() const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    return {
        {"llm", count_pinned_servers_in_pool(ModelType::LLM, ResidencyClass::Standard)},
        {"embedding", count_pinned_servers_in_pool(ModelType::EMBEDDING, ResidencyClass::Standard)},
        {"reranking", count_pinned_servers_in_pool(ModelType::RERANKING, ResidencyClass::Standard)},
        {"transcription", count_pinned_servers_in_pool(ModelType::TRANSCRIPTION, ResidencyClass::Standard)},
        {"image", count_pinned_servers_in_pool(ModelType::IMAGE, ResidencyClass::Standard)},
        {"tts", count_pinned_servers_in_pool(ModelType::TTS, ResidencyClass::Standard)},
        {"classification", count_pinned_servers_in_pool(ModelType::CLASSIFICATION, ResidencyClass::Standard)}
    };
}

json Router::get_pinned_helper_counts() const {
    std::lock_guard<std::mutex> lock(load_mutex_);
    return {
        {"llm", count_pinned_servers_in_pool(ModelType::LLM, ResidencyClass::RoutingHelper)},
        {"embedding", count_pinned_servers_in_pool(ModelType::EMBEDDING, ResidencyClass::RoutingHelper)},
        {"reranking", count_pinned_servers_in_pool(ModelType::RERANKING, ResidencyClass::RoutingHelper)},
        {"transcription", count_pinned_servers_in_pool(ModelType::TRANSCRIPTION, ResidencyClass::RoutingHelper)},
        {"image", count_pinned_servers_in_pool(ModelType::IMAGE, ResidencyClass::RoutingHelper)},
        {"tts", count_pinned_servers_in_pool(ModelType::TTS, ResidencyClass::RoutingHelper)},
        {"classification", count_pinned_servers_in_pool(ModelType::CLASSIFICATION, ResidencyClass::RoutingHelper)}
    };
}

void Router::set_model_pinned(const std::string& model_name, bool pinned) {
    std::unique_lock<std::mutex> lock(load_mutex_);
    std::string canonical_model_name;
    while (true) {
        canonical_model_name =
            resolve_model_name_after_mutation_gate_locked(lock, model_name);
        load_cv_.wait(lock, [this, &canonical_model_name] {
            auto pending = pending_reload_states_.find(canonical_model_name);
            const bool mutation_in_progress =
                pending != pending_reload_states_.end() &&
                pending->second.phase == PendingReloadPhase::Cancelled;
            return !all_model_mutation_active_ && !mutation_in_progress &&
                   active_load_model_name_ != canonical_model_name &&
                   (!exclusive_active_ ||
                    exclusive_owner_ == std::this_thread::get_id());
        });
        if (resolve_model_name(model_name) == canonical_model_name) {
            break;
        }
    }
    WrappedServer* server =
        find_server_by_model_name(canonical_model_name);
    if (!server) {
        auto pending = pending_reload_states_.find(canonical_model_name);
        if (pending != pending_reload_states_.end() &&
            pending->second.phase == PendingReloadPhase::Pending) {
            pending->second.pinned = pinned;
            return;
        }
        throw std::runtime_error("Model not loaded: " + model_name);
    }
    server->set_pinned(pinned);
    auto pending = pending_reload_states_.find(canonical_model_name);
    if (pending != pending_reload_states_.end() &&
        pending->second.phase == PendingReloadPhase::Pending) {
        pending->second.pinned = pinned;
    }
}

} // namespace lemon
