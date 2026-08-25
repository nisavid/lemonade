#include "lemon/config_file.h"
#include "lemon/router.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

namespace lemon {

struct BlockingBackendState {
    BlockingBackendState()
        : release(release_promise.get_future().share()) {}

    std::promise<void> load_entered;
    std::atomic<bool> load_entered_signaled{false};
    std::promise<void> release_promise;
    std::shared_future<void> release;
    std::atomic<int> unload_count{0};
    std::atomic<bool> throw_on_unload{false};
};

class BlockingWrappedServer : public WrappedServer {
public:
    explicit BlockingWrappedServer(
        std::shared_ptr<BlockingBackendState> state)
        : WrappedServer("stub", "error", nullptr, nullptr),
          state_(std::move(state)) {}

    void load(
        const std::string&,
        const ModelInfo&,
        const RecipeOptions&,
        bool) override {
        if (!state_->load_entered_signaled.exchange(true)) {
            state_->load_entered.set_value();
        }
        state_->release.wait();
    }

    void unload() override {
        ++state_->unload_count;
        if (state_->throw_on_unload.load()) {
            throw std::runtime_error("stub unload failure");
        }
    }

    bool is_backend_alive() const override { return true; }

    void become_ready(
        const std::string& model_name,
        nlohmann::json options = nlohmann::json::object()) {
        set_model_metadata(
            model_name, "", ModelType::LLM, DEVICE_CPU,
            RecipeOptions("stub", std::move(options)));
        set_state(ModelState::READY);
    }

private:
    std::shared_ptr<BlockingBackendState> state_;
};

struct RouterModelLifecycleTestHook {
    static void set_backend(
        Router& router,
        std::shared_ptr<BlockingBackendState> state) {
        router.backend_server_factory_ =
            [state = std::move(state)](const ModelInfo&) {
                return std::make_unique<BlockingWrappedServer>(state);
            };
    }

    static void clear_backend(Router& router) {
        router.backend_server_factory_ = {};
    }

    static void add_ready_server(
        Router& router,
        const std::string& model_name,
        nlohmann::json options = nlohmann::json::object(),
        std::shared_ptr<BlockingBackendState> state = nullptr) {
        if (!state) {
            state = std::make_shared<BlockingBackendState>();
        }
        auto server =
            std::make_unique<BlockingWrappedServer>(std::move(state));
        server->become_ready(model_name, std::move(options));
        std::lock_guard<std::mutex> lock(router.load_mutex_);
        router.loaded_servers_.push_back(std::move(server));
    }

    static bool mutation_cancelled(
        Router& router,
        const std::string& model_name) {
        std::lock_guard<std::mutex> lock(router.load_mutex_);
        auto pending = router.pending_reload_states_.find(model_name);
        return pending != router.pending_reload_states_.end() &&
               pending->second.phase ==
                   Router::PendingReloadPhase::Cancelled;
    }

    static bool exclusive_request_pending(Router& router) {
        std::lock_guard<std::mutex> lock(router.load_mutex_);
        return router.exclusive_pending();
    }

    static void seed_watchdog_state(
        Router& router,
        const std::string& model_name,
        bool pinned,
        ResidencyClass residency_class) {
        std::lock_guard<std::mutex> lock(router.load_mutex_);
        router.pending_reload_states_[model_name] = {
            ++router.next_load_generation_,
            Router::PendingReloadPhase::Pending,
            "",
            "stub",
            RecipeOptions("stub", nlohmann::json::object()),
            pinned,
            residency_class};
    }

    static ResidencyClass pending_residency(
        Router& router,
        const std::string& model_name) {
        std::lock_guard<std::mutex> lock(router.load_mutex_);
        return router.pending_reload_states_.at(model_name).residency_class;
    }

    static bool loaded_pinned(
        Router& router,
        const std::string& model_name) {
        std::lock_guard<std::mutex> lock(router.load_mutex_);
        auto* server = router.find_server_by_model_name(model_name);
        return server && server->is_pinned();
    }

    static ModelState loaded_state(
        Router& router,
        const std::string& model_name) {
        std::lock_guard<std::mutex> lock(router.load_mutex_);
        auto* server = router.find_server_by_model_name(model_name);
        return server ? server->get_state() : ModelState::UNLOADED;
    }

};

}  // namespace lemon

namespace {

using namespace std::chrono_literals;

lemon::ModelInfo model_info(const std::string& name) {
    lemon::ModelInfo info;
    info.model_name = name;
    info.recipe = "stub";
    info.type = lemon::ModelType::LLM;
    info.device = lemon::DEVICE_CPU;
    return info;
}

}  // namespace

int main() {
    nlohmann::json values = lemon::ConfigFile::get_defaults();
    values["max_loaded_models"] = 2;
    values["log_level"] = "error";
    values["offline"] = true;
    values["no_fetch_executables"] = true;
    lemon::RuntimeConfig config(values);
    lemon::RuntimeConfig::set_global(&config);
    bool failed = false;
    auto expect = [&failed](bool condition, const char* label) {
        if (!condition) {
            std::cerr << "[FAIL] " << label << '\n';
            failed = true;
        }
    };

    {
        lemon::Router router(&config, nullptr, nullptr);
        auto acquire_exclusive = [&router](
                                     std::atomic<bool>* cancel = nullptr) {
            while (true) {
                auto request = router.request_exclusive(cancel);
                while (request.pending()) {
                    const auto result =
                        router.try_begin_exclusive(request, cancel);
                    if (result ==
                        lemon::Router::ExclusiveAcquireResult::Acquired) {
                        return true;
                    }
                    if (result !=
                        lemon::Router::ExclusiveAcquireResult::Retry) {
                        return false;
                    }
                    std::this_thread::sleep_for(1ms);
                }
                if (request.result() !=
                    lemon::Router::ExclusiveAcquireResult::Retry) {
                    return false;
                }
                std::this_thread::sleep_for(1ms);
            }
        };
        auto preparation = router.prepare_model_load(
            "lifecycle.model", lemon::LoadPurpose::UserInference);
        auto mutation_acquired = std::async(std::launch::async, [&] {
            return router.begin_model_runtime_mutation("lifecycle.model");
        });

        bool cancelled = false;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            if (!router.is_model_tracked("lifecycle.model")) {
                cancelled = true;
                break;
            }
            std::this_thread::sleep_for(1ms);
        }
        failed |= !cancelled;
        failed |= mutation_acquired.wait_for(0ms) !=
                  std::future_status::timeout;

        bool stale_load_rejected = false;
        try {
            auto info = model_info("lifecycle.model");
            router.load_prepared_model(
                std::move(preparation), info,
                lemon::RecipeOptions(
                    info.recipe, nlohmann::json::object()));
        } catch (const std::exception& error) {
            stale_load_rejected =
                std::string(error.what()).find("cancelled or superseded") !=
                std::string::npos;
        }
        failed |= !stale_load_rejected;

        std::future<lemon::Router::PreparedModelLoad> next_preparation;
        {
            auto guard = mutation_acquired.get();
            failed |= router.is_model_tracked("lifecycle.model");
            std::promise<void> next_preparation_started;
            next_preparation = std::async(std::launch::async, [&] {
                next_preparation_started.set_value();
                return router.prepare_model_load(
                    "lifecycle.model", lemon::LoadPurpose::UserInference);
            });
            next_preparation_started.get_future().wait();
            failed |= next_preparation.wait_for(100ms) !=
                      std::future_status::timeout;
        }
        auto after_mutation = next_preparation.get();
        router.abandon_prepared_model_load(std::move(after_mutation));

        auto startup_state =
            std::make_shared<lemon::BlockingBackendState>();
        auto startup_entered = startup_state->load_entered.get_future();
        lemon::RouterModelLifecycleTestHook::set_backend(
            router, startup_state);
        auto startup_preparation = router.prepare_model_load(
            "startup.model", lemon::LoadPurpose::UserInference);
        auto startup_load = std::async(
            std::launch::async,
            [&router,
             preparation = std::move(startup_preparation)]() mutable {
                try {
                    auto info = model_info("startup.model");
                    router.load_prepared_model(
                        std::move(preparation), info,
                        lemon::RecipeOptions(
                            info.recipe, nlohmann::json::object()));
                } catch (const std::exception& error) {
                    return std::string(error.what()).find(
                               "cancelled or superseded") !=
                           std::string::npos;
                }
                return false;
            });
        startup_entered.wait();
        auto startup_mutation = std::async(
            std::launch::async, [&router] {
                return router.begin_model_runtime_mutation(
                    "startup.model");
            });
        bool startup_cancelled = false;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            if (lemon::RouterModelLifecycleTestHook::mutation_cancelled(
                    router, "startup.model")) {
                startup_cancelled = true;
                break;
            }
            std::this_thread::sleep_for(1ms);
        }
        failed |= !startup_cancelled;
        failed |= startup_mutation.wait_for(0ms) !=
                  std::future_status::timeout;
        startup_state->release_promise.set_value();
        failed |= !startup_load.get();
        {
            auto startup_guard = startup_mutation.get();
            failed |= router.is_model_loaded("startup.model");
            failed |= startup_state->unload_count.load() != 1;
        }
        lemon::RouterModelLifecycleTestHook::clear_backend(router);

        lemon::RouterModelLifecycleTestHook::add_ready_server(
            router, "collection.component");
        auto reused_component = router.prepare_model_load(
            "collection.component", lemon::LoadPurpose::UserInference,
            lemon::Router::ExistingModelPolicy::ReuseExisting);
        expect(
            reused_component.already_loaded(),
            "reuse policy keeps a live collection component");
        auto reconciled_component = router.prepare_model_load(
            "collection.component", lemon::LoadPurpose::UserInference,
            lemon::Router::ExistingModelPolicy::ReconcileOptions);
        expect(
            !reconciled_component.already_loaded(),
            "reconcile policy opens a live component for option changes");
        router.abandon_prepared_model_load(
            std::move(reconciled_component));

        lemon::RouterModelLifecycleTestHook::seed_watchdog_state(
            router, "former-helper.direct", false,
            lemon::ResidencyClass::RoutingHelper);
        auto direct_recovery = router.prepare_model_load(
            "former-helper.direct", lemon::LoadPurpose::UserInference);
        expect(
            lemon::RouterModelLifecycleTestHook::pending_residency(
                router, "former-helper.direct") ==
                lemon::ResidencyClass::Standard,
            "direct recovery applies direct residency");
        router.abandon_prepared_model_load(std::move(direct_recovery));

        lemon::RouterModelLifecycleTestHook::seed_watchdog_state(
            router, "former-standard.helper", false,
            lemon::ResidencyClass::Standard);
        auto helper_recovery = router.prepare_model_load(
            "former-standard.helper",
            lemon::LoadPurpose::RoutingDependency);
        expect(
            lemon::RouterModelLifecycleTestHook::pending_residency(
                router, "former-standard.helper") ==
                lemon::ResidencyClass::RoutingHelper,
            "routing recovery applies helper residency");
        router.abandon_prepared_model_load(std::move(helper_recovery));

        lemon::RouterModelLifecycleTestHook::seed_watchdog_state(
            router, "mid-request.watchdog", false,
            lemon::ResidencyClass::RoutingHelper);
        auto watchdog_recovery = router.prepare_model_load(
            "mid-request.watchdog", lemon::LoadPurpose::UserInference,
            lemon::Router::ExistingModelPolicy::ReuseExisting,
            lemon::Router::RecoveryResidencyPolicy::PreserveWatchdog);
        expect(
            lemon::RouterModelLifecycleTestHook::pending_residency(
                router, "mid-request.watchdog") ==
                lemon::ResidencyClass::RoutingHelper,
            "mid-request watchdog recovery preserves residency");
        router.abandon_prepared_model_load(std::move(watchdog_recovery));

        config.set({
            {"pinned_models", nlohmann::json::array({"watchdog.saved-pin"})}});
        auto saved_pin_state =
            std::make_shared<lemon::BlockingBackendState>();
        saved_pin_state->release_promise.set_value();
        lemon::RouterModelLifecycleTestHook::set_backend(
            router, saved_pin_state);
        lemon::RouterModelLifecycleTestHook::seed_watchdog_state(
            router, "watchdog.saved-pin", false,
            lemon::ResidencyClass::Standard);
        auto saved_pin_recovery = router.prepare_model_load(
            "watchdog.saved-pin", lemon::LoadPurpose::UserInference);
        auto saved_pin_info = model_info("watchdog.saved-pin");
        router.load_prepared_model(
            std::move(saved_pin_recovery), saved_pin_info,
            lemon::RecipeOptions(
                saved_pin_info.recipe, nlohmann::json::object()));
        expect(
            lemon::RouterModelLifecycleTestHook::loaded_pinned(
                router, "watchdog.saved-pin"),
            "saved pin preference applies when watchdog recovery commits");
        router.unload_model("watchdog.saved-pin");
        lemon::RouterModelLifecycleTestHook::clear_backend(router);
        config.set({{"pinned_models", nlohmann::json::array()}});

        auto unload_all_preparation = router.prepare_model_load(
            "unload-all.model", lemon::LoadPurpose::UserInference);
        auto unload_all = std::async(std::launch::async, [&] {
            router.unload_model();
        });
        bool unload_all_cancelled = false;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            if (!router.is_model_tracked("unload-all.model")) {
                unload_all_cancelled = true;
                break;
            }
            std::this_thread::sleep_for(1ms);
        }
        failed |= !unload_all_cancelled;
        failed |= unload_all.wait_for(0ms) != std::future_status::timeout;
        bool unload_all_stale_load_rejected = false;
        try {
            auto info = model_info("unload-all.model");
            router.load_prepared_model(
                std::move(unload_all_preparation), info,
                lemon::RecipeOptions(
                    info.recipe, nlohmann::json::object()));
        } catch (const std::exception& error) {
            unload_all_stale_load_rejected =
                std::string(error.what()).find("cancelled or superseded") !=
                std::string::npos;
        }
        failed |= !unload_all_stale_load_rejected;
        unload_all.get();

        failed |= !acquire_exclusive();
        auto exclusive_preparation = router.prepare_model_load(
            "exclusive.model", lemon::LoadPurpose::UserInference);
        std::promise<void> exclusive_loader_entered;
        std::thread blocked_loader([
            &router,
            &exclusive_loader_entered,
            preparation = std::move(exclusive_preparation)]() mutable {
            exclusive_loader_entered.set_value();
            try {
                auto info = model_info("exclusive.model");
                router.load_prepared_model(
                    std::move(preparation), info,
                    lemon::RecipeOptions(
                        info.recipe, nlohmann::json::object()));
            } catch (const std::exception&) {
            }
        });
        exclusive_loader_entered.get_future().wait();
        {
            auto exclusive_guard =
                router.begin_model_runtime_mutation("exclusive.model");
            auto independent_guard =
                router.begin_model_runtime_mutation("independent.model");
            blocked_loader.join();
        }
        router.end_exclusive();

        auto same_thread_preparation = router.prepare_model_load(
            "exclusive-order.model", lemon::LoadPurpose::UserInference);
        auto same_thread_request = router.request_exclusive();
        expect(
            same_thread_request.pending(),
            "exclusive request publishes pending writer intent");
        expect(
            router.try_begin_exclusive(same_thread_request) ==
                lemon::Router::ExclusiveAcquireResult::Retry,
            "exclusive acquisition retries around an outstanding preparation");

        std::promise<void> gated_preparation_started;
        auto gated_preparation = std::async(
            std::launch::async, [&] {
                gated_preparation_started.set_value();
                return router.prepare_model_load(
                    "exclusive-gated.model",
                    lemon::LoadPurpose::UserInference);
            });
        gated_preparation_started.get_future().wait();
        expect(
            gated_preparation.wait_for(100ms) ==
                std::future_status::timeout,
            "pending exclusive request blocks fresh preparations");

        router.abandon_prepared_model_load(
            std::move(same_thread_preparation));
        expect(
            router.try_begin_exclusive(same_thread_request) ==
                lemon::Router::ExclusiveAcquireResult::Acquired,
            "exclusive acquisition succeeds after preparation release");
        auto nested_request = router.request_exclusive();
        expect(
            nested_request.result() ==
                lemon::Router::ExclusiveAcquireResult::InvalidOrder,
            "exclusive acquisition rejects same-thread nesting");
        router.end_exclusive();
        auto after_exclusive = gated_preparation.get();
        router.abandon_prepared_model_load(
            std::move(after_exclusive));

        auto transferred_preparation = router.prepare_model_load(
            "exclusive-transfer.model", lemon::LoadPurpose::UserInference);
        auto transferred_request = router.request_exclusive();
        auto transferred_result = std::async(
            std::launch::async,
            [&router,
             preparation = std::move(transferred_preparation),
             request = std::move(transferred_request)]() mutable {
                const auto pending_result =
                    router.try_begin_exclusive(request);
                router.abandon_prepared_model_load(
                    std::move(preparation));
                const auto acquired_result =
                    router.try_begin_exclusive(request);
                if (acquired_result ==
                    lemon::Router::ExclusiveAcquireResult::Acquired) {
                    router.end_exclusive();
                }
                return std::make_pair(
                    pending_result, acquired_result);
            });
        expect(
            transferred_result.wait_for(1s) ==
                std::future_status::ready,
            "transferred preparation cannot deadlock exclusive acquisition");
        expect(
            transferred_result.get() == std::make_pair(
                lemon::Router::ExclusiveAcquireResult::Retry,
                lemon::Router::ExclusiveAcquireResult::Acquired),
            "transferred preparation drains without self-waiting");

        auto draining_state =
            std::make_shared<lemon::BlockingBackendState>();
        auto draining_entered = draining_state->load_entered.get_future();
        lemon::RouterModelLifecycleTestHook::set_backend(
            router, draining_state);
        auto draining_preparation = router.prepare_model_load(
            "exclusive-drain.model", lemon::LoadPurpose::UserInference);
        auto draining_request = router.request_exclusive();
        auto draining_load = std::async(
            std::launch::async,
            [&router,
             preparation = std::move(draining_preparation)]() mutable {
                auto info = model_info("exclusive-drain.model");
                router.load_prepared_model(
                    std::move(preparation), info,
                    lemon::RecipeOptions(
                        info.recipe, nlohmann::json::object()));
            });
        expect(
            draining_entered.wait_for(1s) == std::future_status::ready,
            "pre-intent preparation enters backend startup while intent is pending");
        expect(
            router.try_begin_exclusive(draining_request) ==
                lemon::Router::ExclusiveAcquireResult::Retry,
            "exclusive acquisition waits for pre-intent publication");
        draining_state->release_promise.set_value();
        draining_load.get();
        expect(
            router.try_begin_exclusive(draining_request) ==
                lemon::Router::ExclusiveAcquireResult::Acquired,
            "exclusive acquisition completes after pre-intent publication");
        router.end_exclusive();
        router.unload_model("exclusive-drain.model");
        lemon::RouterModelLifecycleTestHook::clear_backend(router);

        lemon::RouterModelLifecycleTestHook::add_ready_server(
            router,
            "exclusive-maintenance.model",
            {{"auto_evict", true},
             {"downsize_idle_timeout", 0},
             {"evict_idle_timeout", 3600}});
        {
            auto maintenance_request = router.request_exclusive();
            router.simulate_vram_pressure(-1.0);
            expect(
                lemon::RouterModelLifecycleTestHook::loaded_state(
                    router, "exclusive-maintenance.model") ==
                    lemon::ModelState::READY,
                "pending exclusive request blocks maintenance admission");
        }
        router.unload_model("exclusive-maintenance.model");

        auto throwing_mutation_state =
            std::make_shared<lemon::BlockingBackendState>();
        throwing_mutation_state->throw_on_unload = true;
        lemon::RouterModelLifecycleTestHook::add_ready_server(
            router, "throwing-mutation.model", nlohmann::json::object(),
            throwing_mutation_state);
        bool mutation_unload_failed = false;
        try {
            auto guard = router.begin_model_runtime_mutation(
                "throwing-mutation.model");
        } catch (const std::exception& error) {
            mutation_unload_failed =
                std::string(error.what()) == "stub unload failure";
        }
        expect(
            mutation_unload_failed,
            "lifecycle mutation propagates backend unload failure");

        auto throwing_discard_state =
            std::make_shared<lemon::BlockingBackendState>();
        throwing_discard_state->throw_on_unload = true;
        lemon::RouterModelLifecycleTestHook::add_ready_server(
            router, "throwing-discard.model", nlohmann::json::object(),
            throwing_discard_state);
        bool discard_unload_failed = false;
        try {
            router.unload_model("throwing-discard.model");
        } catch (const std::exception& error) {
            discard_unload_failed =
                std::string(error.what()) == "stub unload failure";
        }
        expect(
            discard_unload_failed,
            "runtime discard propagates backend unload failure");

        auto after_failure_request = router.request_exclusive();
        expect(
            router.try_begin_exclusive(after_failure_request) ==
                lemon::Router::ExclusiveAcquireResult::Acquired,
            "failed lifecycle cleanup leaves exclusive acquisition available");
        router.end_exclusive();

        auto after_mutation_failure = router.prepare_model_load(
            "throwing-mutation.model", lemon::LoadPurpose::UserInference);
        expect(
            after_mutation_failure.already_loaded(),
            "failed mutation leaves same-model preparation available");
        router.abandon_prepared_model_load(
            std::move(after_mutation_failure));
        auto after_discard_failure = router.prepare_model_load(
            "throwing-discard.model", lemon::LoadPurpose::UserInference);
        expect(
            after_discard_failure.already_loaded(),
            "failed discard leaves same-model preparation available");
        router.abandon_prepared_model_load(
            std::move(after_discard_failure));

        throwing_mutation_state->throw_on_unload = false;
        throwing_discard_state->throw_on_unload = false;
        router.unload_model("throwing-mutation.model");
        router.unload_model("throwing-discard.model");

        {
            auto mutation_guard =
                router.begin_model_runtime_mutation(
                    "exclusive-mutation.model");
            auto mutation_request = router.request_exclusive();
            expect(
                mutation_request.pending(),
                "exclusive request coexists with an admitted lifecycle mutation");
            expect(
                router.try_begin_exclusive(mutation_request) ==
                    lemon::Router::ExclusiveAcquireResult::Retry,
                "exclusive acquisition yields to an admitted lifecycle mutation");
        }

        std::atomic<bool> cancel_reverse_exclusive = false;
        std::future<bool> cancelled_reverse_exclusive;
        {
            auto mutation_guard =
                router.begin_model_runtime_mutation("cancel-exclusive.model");
            std::promise<void> reverse_exclusive_started;
            cancelled_reverse_exclusive = std::async(
                std::launch::async, [&] {
                    auto request =
                        router.request_exclusive(&cancel_reverse_exclusive);
                    reverse_exclusive_started.set_value();
                    while (request.pending()) {
                        const auto result = router.try_begin_exclusive(
                            request, &cancel_reverse_exclusive);
                        if (result ==
                            lemon::Router::ExclusiveAcquireResult::Acquired) {
                            return true;
                        }
                        if (result !=
                            lemon::Router::ExclusiveAcquireResult::Retry) {
                            return false;
                        }
                        std::this_thread::sleep_for(1ms);
                    }
                    return false;
                });
            reverse_exclusive_started.get_future().wait();
            bool pending_request_observed = false;
            for (int attempt = 0; attempt < 1000; ++attempt) {
                if (lemon::RouterModelLifecycleTestHook::
                        exclusive_request_pending(router)) {
                    pending_request_observed = true;
                    break;
                }
                std::this_thread::sleep_for(1ms);
            }
            expect(
                pending_request_observed,
                "exclusive cancellation observes published writer intent");
            cancel_reverse_exclusive = true;
            failed |= cancelled_reverse_exclusive.wait_for(1s) !=
                      std::future_status::ready;
        }
        const bool cancelled_exclusive_started =
            cancelled_reverse_exclusive.get();
        failed |= cancelled_exclusive_started;
        if (cancelled_exclusive_started) {
            router.end_exclusive();
        }

        std::future<bool> reverse_exclusive;
        {
            auto mutation_guard =
                router.begin_model_runtime_mutation("reverse.model");
            std::promise<void> reverse_exclusive_started;
            reverse_exclusive = std::async(std::launch::async, [&] {
                reverse_exclusive_started.set_value();
                return acquire_exclusive();
            });
            reverse_exclusive_started.get_future().wait();
            failed |= reverse_exclusive.wait_for(100ms) !=
                      std::future_status::timeout;
        }
        failed |= !reverse_exclusive.get();
        router.end_exclusive();
    }

    lemon::RuntimeConfig::set_global(nullptr);
    if (failed) {
        std::cerr << "router model lifecycle contract failed\n";
        return 1;
    }
    return 0;
}
