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
- Busy `WrappedServer` instances are protected from eviction until their active request ends.
- Non-file-not-found load failures trigger an evict-all-and-retry path.
- Exclusive NPU recipes evict other NPU users; FLM has recipe-specific coexistence rules.

## Frontend Split

- `src/app/src/renderer/` is the shared React renderer.
- The Tauri desktop app uses `src/app/src-tauri/` and installs `window.api` through `tauriShim.ts`.
- The browser web app in `src/web-app/` reuses the shared renderer with a separate package/dependency tree.
- Do not consolidate `src/app/package.json` and `src/web-app/package.json`; the split supports Debian native packaging with system Node modules.

## OmniRouter

- `docs/dev/omni-router.md` explains the upstream design intent.
- `src/app/src/renderer/utils/toolDefinitions.json` is the canonical local source for tool definitions.
- Collections are virtual multi-model entries. They are hidden from the default `/v1/models` response and surfaced through `?show_all=true` for the app.

## Before Changing Architecture

1. Read the relevant user-facing docs first to infer intended behavior.
2. Read the source paths above.
3. Check whether the behavior is an upstream invariant, a fork-local policy, or an implementation accident.
4. If the change deliberately alters architecture, record the decision in `docs/adr/` when it is hard to reverse, surprising, and trade-off driven.
