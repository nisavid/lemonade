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

- This accepted fork target is recorded in `docs/adr/0001-adopt-protected-memory-capacity-driven-model-residency.md`.
- Continued fork maintenance is decided. Reconcile onto current stable upstream, then implement this target using upstream terminology and APIs where they fit.
- One server-owned planner defines admission, pressure, protection, ordering, and refusal semantics. Platform and backend adapters describe residency-memory-domain topology, trustworthy capacity and pressure signals, footprint confidence, and available reclamation actions; adapters do not redefine common policy.
- The portable specification covers every upstream-supported GPU platform and backend family conceptually and records a validated, modeled, fallback-only, or unsupported capability level for each residency operation and affected domain set, including at least admission and measured-pressure reclamation. A combination-wide label is a derived display summary, never a policy input. GPU residency is in scope, current NPU coexistence remains in this route, and CPU residency is excluded.
- Capability level records support evidence, not permission to trust a signal forever. The effective residency mode is computed per operation and affected domain set from that operation's required signals. Plans snapshot signal generations and revalidate them when acquiring the plan reservation and before each irreversible action and incoming load; missing, stale, or unhealthy signals select the declared fallback or refusal, and any post-reclamation change stops the remaining plan under the partial-outcome policy.
- Pins consume `max_loaded_models` slots and are never automatic LRU victims. Hatchery disables the count ceiling with `max_loaded_models=-1` and uses the capacity policy as its primary admission bound.
- `recipe_options.json` is the source of durable per-model pin preference. A global `load_pinned_models_on_startup` policy, defaulting to false, controls best-effort startup admission of those models.
- Startup failures are logged and surfaced without preventing `lemond` from starting. A saved pin preference remains visible even when its model is not resident.
- Startup loading preflights the complete saved-pin set. It loads nonconflicting preferences but does not choose a winner from a mutually exclusive NPU/FLM set; every member of that set remains durable and receives a surfaced conflict.
- Hatchery is the first behavioral reference profile and initial validation target, not an implementation freeze or an already validated combination. Its residency memory domain is GTT/shared GPU memory, which accounts for resident model weights and caches; host `MemAvailable` is an independent system-health interlock and is never added to GTT usage.
- On shared-memory systems, admission requires both predicted residency-domain headroom and the post-admission host-memory safety floor. Breaching either is a pressure trigger. The planner evaluates one constraint vector containing every deficient residency domain and the separate host-floor deficit; each action maps a unique physical allocation to its effects on those constraints, so reclaiming shared bytes may improve both without counting them as two allocations.
- Admission is predictive and plans the complete reclamation set before changing residency. Incoming and resident model/cache demand is a per-domain footprint vector derived from measurements or calibration, with confidence reported and an explicit safe fallback for unknown or unattributed demand.
- Pressure reclamation and admission reclamation share eligibility rules. A pinned or in-use model vetoes automatic hard reclamation.
- Soft reclamation may clear reconstructible idle state such as a KV cache while preserving weights, the backend process, and any pin.
- Hard reclamation considers cold unpinned idle models before warm unpinned idle models. Select a complete feasible action set before execution; recovery utility is capped by each remaining constraint deficit, and unrelated or excess capacity receives no credit. Preserve cold-before-warm and greatest-useful-recovery semantics; scoring and deterministic tie-break details remain implementation design. Age alone does not trigger hard reclamation when there is no admission or measured pressure.
- NPU/FLM conflict planning resolves the effective device and slot policy before eviction. If any conflicting resident is pinned or in use, reject the incoming load without changing residency.
- A pinned newcomer has no precedence over a pinned incumbent. Reconfiguring a live pinned model requires explicit unpin or an explicit force operation.
- Load-failure retry may reclaim only eligible unpinned idle models. It never performs an unconditional evict-all retry.
- Manual unload, explicit force, service termination, dead-backend pruning, and owner-scoped job cleanup remain distinct explicit or lifecycle operations.
- Runtime pinning follows upstream load and pin semantics. Durable pin persistence is a separate per-model recipe-options operation that patches only `pinned` and preserves every unrelated saved option.
- Every request acquisition and release, runtime pin change, manual lifecycle action, backend-state transition, and automatic planner uses one server-owned per-resident state machine. An `idle` to `soft-reclaiming` action lease requires the resident not be in use but may preserve a pin; an `idle` to `hard-reclaiming` lease requires it be unpinned and not in use. New inference, pin, and lifecycle acquisition must wait or receive an explicit retry or refusal while an action lease owns the resident.
- A plan coordinator atomically reserves the full incoming per-domain footprint and host effect, NPU/FLM compatibility slots, and the complete ordered action-lease set before the first side effect. It acquires multi-resource reservations in deterministic order, all competing plans and acquisitions contend with it, and it holds the reservation through a terminal plan outcome. Admission, measured pressure, NPU conflict, and load-failure retry all use this boundary and abandon stale plans before mutation.
- Reclamation side effects remain fallible even when the decision is atomic. Execute victims one at a time; on the first failure, stop the remaining plan, do not start the incoming load, preserve untouched residents, report the successful and failed actions, and do not automatically reload already reclaimed models.
- Reconcile every action before crediting recovered capacity. Verified release transitions to `unloaded`; a verified-intact failure returns to the appropriate resident state; an ambiguous or unreachable backend transitions to a conservatively accounted `quarantined` state. Every action lease ends through one of these state transitions, and admission never relies on capacity whose release is unverified.
- If admission cannot create a safe plan, or measured pressure remains after all eligible actions, refuse or stop and report the unresolved condition rather than reclaiming a protected model or making arbitrary extra changes.
- `lemond` owns the operational explanation contract. Structured API state and logs use one stable plan id and report the trigger, initiating request or client when applicable, starting resident state and signal generations, per-operation capability and effective mode, constraints, incoming demand and footprint confidence, ordered proposed and actual actions, per-action outcomes, refusals, fallbacks, partial outcomes, terminal outcome, and reason codes; clients only render that server-owned truth.
- Public server configuration stays small and intent-oriented. Sensor selection, calibration details, victim-planning internals, and similar mechanisms are derived unless an operator-facing need is demonstrated.
- These behavioral guarantees are fixed for the portable specification; sensor sources, topology discovery, footprint estimation, thresholds, hysteresis, polling, cold/warm classification, adapter shapes, public names, and concrete validation scenarios remain implementation-design work.
- The implementation handoff must include a platform/backend matrix with per-operation and affected-domain-set capability cells, primary evidence, effective fallback or refusal behavior, and the tests required to advance each cell.

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
