# Architecture Map

This file captures implementation landmarks that are useful before changing Lemonade, but too implementation-specific for `CONTEXT.md`. Sections describe the checked-out fork unless they are explicitly labelled as an accepted target.

## Process Topology

- `lemond` is the pure HTTP server process.
- `lemonade` is the CLI client.
- `LemonadeServer.exe` embeds `lemond` on Windows and provides the always-on tray icon.
- `lemonade-tray` is the macOS/Linux tray client.
- `lemonade-server` is a deprecated compatibility shim.
- Backends run as subprocesses; do not move inference engines in-process without an explicit architecture decision.

## Server Core

- `src/cpp/server/server.cpp` registers HTTP routes, serves the web app, applies auth, and owns endpoint handlers.
- Core API endpoints are registered under `/api/v0/`, `/api/v1/`, `/v0/`, and `/v1/`.
- Ollama-compatible routes live under `/api/` without the Lemonade/OpenAI version prefix.
- Internal endpoints live under `/internal/`: `shutdown`, `set`, `config`, and `cleanup-cache`. They are loopback-restricted admin/local lifecycle, configuration, and cache-management APIs, not OpenAI-compatible routes.
- The checked-out fork exposes configured pin management as `GET /pins`, `POST /pins`, and `DELETE /pins/{model_name}` under `/api/v0/`, `/api/v1/`, `/v0/`, and `/v1/`.

## Model Lifecycle

- `Router` (`src/cpp/server/router.cpp`) selects loaded servers, applies model-type LRU, enforces NPU constraints, and forwards inference.
- `WrappedServer` (`src/cpp/include/lemon/wrapped_server.h`) is the in-process abstraction for one backend subprocess.
- `ModelManager` (`src/cpp/server/model_manager.cpp`) loads built-in and user registries, resolves paths, registers user models, discovers extra GGUF models, and downloads artifacts.
- `server_models.json` is the built-in model registry.
- `backend_versions.json` pins downloadable backend artifact versions.
- `recipe_options.json` stores per-model runtime options in the Lemonade cache.

## Current Cache and Eviction

- Model type controls the LRU bucket: LLM, embedding, reranking, audio, image, or TTS.
- `max_loaded_models` applies per model type, not globally.
- `max_gpu_memory_occupancy_gb` applies across loaded GPU models. Router dry-runs largest-to-smallest GPU evictions and leaves existing models loaded when the requested model cannot fit.
- Configured pins live in `pinned_models`, are loaded at startup, do not consume count-managed slots, and are excluded from ordinary automatic capacity victims.
- An incoming pinned NPU/FLM model can evict an incompatible pinned incumbent. This last-pinned-load-wins behavior is unsafe and is replaced by the accepted target below.
- Busy `WrappedServer` instances are protected from eviction until their active request ends.
- The load-failure retry path preserves configured pins but can evict every eligible unpinned model before retrying.
- This ref has no upstream-style reactive pressure engine or idle KV-cache reclamation.

## Accepted Model Residency Target

- Continued fork maintenance is decided. Reconcile onto current stable upstream, then implement this target using upstream terminology and APIs where they fit.
- Pins consume `max_loaded_models` slots and are never automatic LRU victims. Hatchery disables the count ceiling with `max_loaded_models=-1` and uses the capacity policy as its primary admission bound.
- `recipe_options.json` is the source of durable per-model pin preference. A global `load_pinned_models_on_startup` policy, defaulting to false, controls best-effort startup admission of those models.
- Startup failures are logged and surfaced without preventing `lemond` from starting. A saved pin preference remains visible even when its model is not resident.
- Startup loading preflights the complete saved-pin set. It loads nonconflicting preferences but does not choose a winner from a mutually exclusive NPU/FLM set; every member of that set remains durable and receives a surfaced conflict.
- Hatchery admission uses GTT/shared GPU memory as the model-residency capacity signal. Host `MemAvailable` is an independent system-health interlock and is never added to GTT usage.
- Admission reclamation dry-runs the full candidate plan before changing residency. Estimate model and KV-cache GTT demand from measured or calibrated footprints, with an explicit fallback for unknown models.
- Pressure reclamation and admission reclamation share eligibility rules. A pinned or in-use model vetoes automatic hard reclamation.
- Soft reclamation may clear reconstructible idle state such as a KV cache while preserving weights, the backend process, and any pin.
- Hard-reclamation order is largest cold unpinned idle model first, then largest warm unpinned idle model. Age alone does not trigger hard reclamation when there is no admission or measured pressure.
- NPU/FLM conflict planning resolves the effective device and slot policy before eviction. If any conflicting resident is pinned or in use, reject the incoming load without changing residency.
- A pinned newcomer has no precedence over a pinned incumbent. Reconfiguring a live pinned model requires explicit unpin or an explicit force operation.
- Load-failure retry may reclaim only eligible unpinned idle models. It never performs an unconditional evict-all retry.
- Manual unload, explicit force, service termination, dead-backend pruning, and owner-scoped job cleanup remain distinct explicit or lifecycle operations.
- Runtime pinning follows upstream load and pin semantics. Durable pin persistence is a separate per-model recipe-options operation that patches only `pinned` and preserves every unrelated saved option.

## Frontend Split

- `src/app/src/renderer/` is the shared React renderer.
- The Tauri desktop app uses `src/app/src-tauri/` and installs `window.api` through `tauriShim.ts`.
- The browser web app in `src/web-app/` reuses the shared renderer with a separate package/dependency tree.
- Do not consolidate `src/app/package.json` and `src/web-app/package.json`; the split supports Debian native packaging with system Node modules.

### Accepted residency UI target

- Runtime pin state, saved pin preference, and startup-loading policy use distinct controls and labels. A pin action never silently changes the startup policy.
- Saved pin preferences and startup failures remain visible when their models are not loaded so users can retry, change options, or remove the preference.
- The startup-loading setting is server-owned lifecycle configuration. It must not move into per-client desktop or web-app settings.
- Residency rows surface loaded, in-use, pinned, remembered-pin, loading, soft-reclaimed, pressure-reclaimed, and refusal states with text or accessible names in addition to color.
- Resource telemetry distinguishes GTT/shared GPU memory from host memory on platforms where that distinction is meaningful.
- Follow `DESIGN.md` and `.impeccable/design.json` for the Impeccable v4 product language. Current CSS that contradicts those documents is implementation debt, not a design-system exception.

## OmniRouter

- `docs/dev/omni-router.md` explains the upstream design intent.
- `src/app/src/renderer/utils/toolDefinitions.json` is the canonical local source for tool definitions.
- Collections are virtual multi-model entries. They are hidden from the default `/v1/models` response and surfaced through `?show_all=true` for the app.

## Before Changing Architecture

1. Read the relevant user-facing docs first to infer intended behavior.
2. Read the source paths above.
3. Check whether the behavior is an upstream invariant, a fork-local policy, or an implementation accident.
4. If the change deliberately alters architecture, record the decision in `docs/adr/` when it is hard to reverse, surprising, and trade-off driven.
