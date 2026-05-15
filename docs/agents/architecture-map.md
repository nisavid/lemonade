# Architecture Map

This file captures implementation landmarks that are useful before changing Lemonade, but too implementation-specific for `CONTEXT.md`.

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
- Model pin management belongs on narrow Lemonade API routes under the normal version prefixes, not on generic `/internal/set` config editing. The first route shape is `GET /pins`, `POST /pins`, and `DELETE /pins/{model_name}` under `/api/v0/`, `/api/v1/`, `/v0/`, and `/v1/`. `GET /pins` returns configured pins with loaded status and optional load error.

## Model Lifecycle

- `Router` (`src/cpp/server/router.cpp`) selects loaded servers, applies model-type LRU, enforces NPU constraints, and forwards inference.
- `WrappedServer` (`src/cpp/include/lemon/wrapped_server.h`) is the in-process abstraction for one backend subprocess.
- `ModelManager` (`src/cpp/server/model_manager.cpp`) loads built-in and user registries, resolves paths, registers user models, discovers extra GGUF models, and downloads artifacts.
- `server_models.json` is the built-in model registry.
- `backend_versions.json` pins downloadable backend artifact versions.
- `recipe_options.json` stores per-model runtime options in the Lemonade cache.

## Cache and Eviction

- Model type controls the LRU bucket: LLM, embedding, reranking, audio, image, or TTS.
- `max_loaded_models` applies per model type, not globally.
- `max_gpu_memory_occupancy_gb` applies across loaded GPU models. Router dry-runs largest-to-smallest GPU evictions and leaves existing models loaded when the requested model cannot fit.
- Model pins are excluded from automatic eviction policies and from eviction-managed capacity counts. Their resident GPU usage still reduces physical memory available to unpinned loads.
- Model pin loading is best-effort at `lemond` startup; failed pin loads should be logged and surfaced to clients without preventing the server from starting.
- Pinning an already-loaded model is a live metadata/config transition; it should persist the server config and protect the current `WrappedServer` without unloading or reloading it.
- `POST /pins` accepts loaded or loading non-collection models; it is idempotent for models that are already pinned.
- `DELETE /pins/{model_name}` persists config and clears protection on the current `WrappedServer` if it is loaded, without unloading it.
- Model pin configuration stores canonical model names only; recipe options remain owned by the existing recipe-options path.
- Explicit unload affects current residency only; unpinning is the action that removes a model from pin configuration.
- Deleting a model removes that model name from pin configuration.
- Collection ids are not pinned; users pin the concrete loaded component models instead.
- NPU exclusivity still applies between model pins; startup pin loading processes pins in configured order, so a later conflicting pinned NPU model can evict an earlier one instead of preserving all pins resident.
- `pinned_models` preserves insertion order and `lemond` loads pins in that order; drag-to-reorder is optional UI polish, not required for the first implementation.
- Busy `WrappedServer` instances are protected from eviction until their active request ends.
- Non-file-not-found load failures trigger an evict-all-and-retry path.
- Exclusive NPU recipes evict other NPU users; FLM has recipe-specific coexistence rules.

## Frontend Split

- `src/app/src/renderer/` is the shared React renderer.
- The Tauri desktop app uses `src/app/src-tauri/` and installs `window.api` through `tauriShim.ts`.
- The browser web app in `src/web-app/` reuses the shared renderer with a separate package/dependency tree.
- Do not consolidate `src/app/package.json` and `src/web-app/package.json`; the split supports Debian native packaging with system Node modules.
- Model pin controls belong at the leading edge of active-model list rows, not in the trailing unload/action button cluster.
- The active-model UI should surface configured pinned models even when they are not currently loaded, so users can see failed/manual-unloaded pins and unpin them.

## OmniRouter

- `docs/dev/omni-router.md` explains the upstream design intent.
- `src/app/src/renderer/utils/toolDefinitions.json` is the canonical local source for tool definitions.
- Collections are virtual multi-model entries. They are hidden from the default `/v1/models` response and surfaced through `?show_all=true` for the app.

## Before Changing Architecture

1. Read the relevant user-facing docs first to infer intended behavior.
2. Read the source paths above.
3. Check whether the behavior is an upstream invariant, a fork-local policy, or an implementation accident.
4. If the change deliberately alters architecture, record the decision in `docs/adr/` when it is hard to reverse, surprising, and trade-off driven.
