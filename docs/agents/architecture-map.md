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
- The accepted portable adapter contract and its decision rationale live in [Choose portable residency adapter contracts](https://github.com/nisavid/lemonade/issues/31); the bullets below are its scout map.
- Continued fork maintenance is decided. Reconcile onto current stable upstream, then implement this target using upstream terminology and APIs where they fit.
- One server-owned planner defines admission, pressure, protection, ordering, and refusal semantics. Platform and backend adapters describe residency-memory-domain topology, trustworthy capacity and pressure signals, footprint confidence, and available reclamation actions; adapters do not redefine common policy.
- The portable contract has three authority domains. Platform providers report hardware/OS topology, observations, health, and containment primitives. Backend adapters report backend/device resolution, lifecycle-shaped predicted and observed effects, raw mechanism availability, and launch requirements. The server-owned control plane validates every envelope, issues canonical identities, compiles actions, owns the capability catalog, lifecycle mutation gate, constraint ledger, planner, and recovery subsystem, and makes every policy decision.
- Every adapter family uses a centrally validated versioned envelope. Unknown majors, required features, kinds, provenance, or affected scopes fail closed; unknown data never becomes zero capacity, an empty claim set, a successful no-op action, or a healthy default.
- The composite topology resolver keeps physical backing resources, residency memory domains, provider measurement views, and operational constraint instances distinct. It alone canonicalizes aliases and correlation evidence. A topology change fences new effects, drains or reconciles old generation tokens, retains old claims, and atomically rebinds exact identities; ambiguous mappings become shared-unknown quarantine rather than independent capacity.
- Predicted and observed resource effects use server-issued typed identities and explicit logical-to-observed allocation-group reconciliation. Effects distinguish persistent weights, fixed cache, reconstructible state, transient workspace, allocator reserve, and host effects. Every effect carries its complete atomic claim set; splitting, coalescing, and shared allocations must be proven without double counting.
- A reviewed capability catalog derives footprint confidence, required observations and freshness, recovery and discovery mechanisms, reclamation protocols, effective capability, fallback, and refusal from attested runtime evidence. The compiled catalog, discovery profiles, mechanism contracts, and applied evidence are immutable content-addressed recovery objects. Journal references bind their exact versions and cannot be reinterpreted more permissively after reload or restart.
- One lifecycle mutation gate composes owner-lease fencing and the topology, catalog, discovery, configuration, observation, daemon, and journal generations. Ordinary lifecycle work requires a fully ready token. A scoped recovery-remediation token may only strengthen quarantine, rebind evidence, perform authorized cleanup, verify release, and catch up the ledger; it cannot route, admit, grow, weaken claims, or bypass verified-release rules.
- The common claim algebra has four closed kinds: consumable capacity, safety floor, cardinality pool, and compatibility/exclusivity. A server-built completeness manifest covers every applicable claim before the first side effect. Initial runtime realization may only narrow the pre-spawn envelope; later growth requires a separately cataloged, externally gated transaction that reserves and journals a conservative superset before the effect.
- The durable recovery/claim journal is the only persisted claim authority; the runtime ledger is solely its ordered projection. Live processes and allocations are reconciliation evidence, never a second authority or automatically adopted residency. Bootstrap proves complete coverage only for independently marked or plausibly Lemonade-owned resources; unrelated applications remain external capacity/pressure observations.
- Every resource-owning launch prepares a journal record before spawn and uses either hereditary containment or externally mediated durable membership. Prepared nonce, maximum complete claims, catalog/evidence identities, and intended scope bind atomically to process creation before resource ownership. Envelope violations invalidate the bound, fence the resident, and quarantine the complete plausible claim set.
- Restart recovery is cleanup-only. Every non-released prior-epoch record becomes non-routable quarantine. After the prior owner is proven dead or fully effect-fenced, `owner_scoped_cleanup` ignores stale prior-epoch pin/in-use metadata, uses the last proven recovery authority, and retains every claim until complete release is verified. Ambiguous ownership is never signaled.
- The journal validates a closed launch-phase/disposition transition table. Known-owned and fully bounded policy invalidation enters `suspended` immediately; only same-epoch full re-attestation may restore it to active. Any ambiguity enters `quarantined`, which can exit only to verified `released`.
- A finite pre-spawn candidate set compiles to the union of all candidate claims, maxima, prerequisites, and observation dependencies under the most conservative thresholds and least-permissive disposition. Every candidate must authorize the discovery spawn under one recovery authority. Post-spawn realization must match exactly one cell and may narrow but never enlarge the prepared permission or reservation.
- The portable specification covers every upstream-supported GPU platform and backend family conceptually and records a validated, modeled, fallback-only, or unsupported capability level for each residency operation and affected domain set, including at least admission and measured-pressure reclamation. A combination-wide label is a derived display summary, never a policy input. GPU residency is in scope, current NPU coexistence remains in this route, and CPU residency is excluded.
- Capability level records support evidence, not permission to trust a signal forever. The effective residency mode is computed per operation and affected domain set from that operation's required signals. Plans snapshot signal generations and revalidate them when acquiring the plan reservation and before each irreversible action and incoming load; missing, stale, or unhealthy signals select the declared fallback or refusal, and any post-reclamation change stops the remaining plan under the partial-outcome policy.
- Pins consume `max_loaded_models` slots and are never automatic LRU victims. Hatchery disables the count ceiling with `max_loaded_models=-1` and uses the capacity policy as its primary admission bound.
- `recipe_options.json` is the source of durable per-model pin preference. A global `load_pinned_models_on_startup` policy, defaulting to false, controls whether `lemond` attempts grouped startup admission of those models.
- Startup failures are logged and surfaced without preventing `lemond` from starting. A saved pin preference remains visible even when its model is not resident.
- Startup loading first resolves every saved preference into an immutable admission intent containing model identity, recipe, backend and device, saved options, topology, count category, effective mode, configuration and signal generations, and predicted constraint effects. It builds connected startup admission sets from those intents, revalidates their identities when reserving a set, and waits or invalidates before mutation if relevant configuration changes.
- Preferences belong to the same startup admission set when their resolved intents share any capacity, host-health, count, compatibility, or adapter-declared constraint. An independent set proceeds only when the planner can jointly admit every member under its current effective modes; an infeasible set loads none, preserves every saved preference, and surfaces a conflict for every member. Registry, JSON, container, and execution order never grant precedence.
- The plan coordinator reserves a complete feasible startup admission set before its first load. Members materialize in deterministic order as owner-scoped, non-routable provisional loads without runtime pins. Each load is reconciled against its intent and reservation before the next begins. Once every member is verified, one atomic commit publishes the complete set as routable residents, converts claims to committed occupancy, and applies runtime pins.
- Any pre-commit load failure, signal or configuration change, cancellation, reconciliation mismatch, or reservation invalidation aborts the set and rolls back every provisional member. Cleanup is transaction-owned rather than automatic reclamation of a committed pin. Reconcile and report partial or quarantined outcomes when rollback cannot restore the pre-attempt state. Other independent feasible sets may still load, and `lemond` still starts.
- Saved preferences are the only durable desired startup state, not the only durable operational recovery state. Before starting lifecycle subsystems, `lemond` acquires an exclusive daemon/recovery-store owner lease for its cache and constraint ledger. A second daemon cannot classify a live owner's children as orphans; while another owner is live or ownership is uncertain, model lifecycle remains not ready and refuses mutation explicitly.
- Before any backend spawn, its platform/backend path durably prepares one recovery-authority record and selects exactly one cataloged mode: hereditary containment or externally mediated durable membership. The record contains the daemon epoch, unique launch nonce, resolved intent, maximum complete constraint claims, applied catalog/evidence identities, and intended containment/tree scope. Process creation atomically binds the nonce or containment identity to the child tree; before the plan advances, the record transitions monotonically to a non-reusable process-start identity and complete descendant scope. PID alone is never ownership proof.
- Every daemon crosses one recovery barrier before starting the startup-pin loader, pressure engine, or any other automatic residency work and before a model-lifecycle endpoint may mutate state. The barrier replays every ownership record into the constraint ledger as maximum-claim quarantine, proves the prior owner dead or fully effect-fenced, and releases claims only after matching the full launch/process identity and verifying complete tree and resource release. It never adopts prior-epoch survivors. Incomplete, malformed, reused-PID, live-owner, or otherwise ambiguous records retain quarantine claims or block the affected lifecycle scope; they block all lifecycle when that scope cannot be proven. Cleanup never signals a process whose ownership is uncertain. Non-lifecycle health may expose recovery progress, while lifecycle requests receive an explicit unavailable or retry response.
- If a platform/backend cannot provide either recovery-authority mode with trustworthy identity, complete ownership coverage, and verified release, its affected operation must use a safe fallback or refuse.
- Hatchery is the first behavioral reference profile and initial validation target, not an implementation freeze or an already validated combination. Its residency memory domain is GTT/shared GPU memory, which accounts for resident model weights and caches; host `MemAvailable` is an independent system-health interlock and is never added to GTT usage.
- On shared-memory systems, admission requires both predicted residency-domain headroom and the post-admission host-memory safety floor. Breaching either is a pressure trigger. The planner evaluates one constraint vector containing every deficient residency domain and the separate host-floor deficit; each action maps a unique physical allocation to its effects on those constraints, so reclaiming shared bytes may improve both without counting them as two allocations.
- Admission is predictive and plans the complete reclamation set before changing residency. Incoming and resident model/cache demand is a per-domain footprint vector derived from measurements or calibration, with confidence reported and an explicit safe fallback for unknown or unattributed demand.
- Pressure reclamation and admission reclamation share eligibility rules. A pinned or in-use model vetoes automatic hard reclamation.
- Soft reclamation may clear reconstructible idle state such as a KV cache while preserving weights, the backend process, and any pin.
- Hard reclamation considers cold unpinned idle models before warm unpinned idle models. Select a complete feasible action set before execution; recovery utility is capped by each remaining constraint deficit, and unrelated or excess capacity receives no credit. Preserve cold-before-warm and greatest-useful-recovery semantics; scoring and deterministic tie-break details remain implementation design. Age alone does not trigger hard reclamation when there is no admission or measured pressure.
- NPU/FLM conflict planning resolves the effective device and slot policy before eviction. If any conflicting resident is pinned or in use, reject the incoming load without changing residency.
- A pinned newcomer has no precedence over a pinned incumbent. Reconfiguring a live pinned model requires explicit unpin or an explicit force operation.
- Load-failure retry may reclaim only eligible unpinned idle models. It never performs an unconditional evict-all retry.
- Manual unload, explicit force, service termination, dead-backend pruning, same-epoch owner-scoped job cleanup, and prior-epoch `owner_scoped_cleanup` remain distinct explicit or lifecycle operations.
- Runtime pinning follows upstream load and pin semantics. Durable pin persistence is a separate per-model recipe-options operation that patches only `pinned` and preserves every unrelated saved option.
- Every request acquisition and release, runtime pin change, manual lifecycle action, backend-state transition, and automatic planner uses one server-owned per-resident state machine. An `idle` to `soft-reclaiming` action lease requires the resident not be in use but may preserve a pin; an `idle` to `hard-reclaiming` lease requires it be unpinned and not in use. New inference, pin, and lifecycle acquisition must wait or receive an explicit retry or refusal while an action lease owns the resident.
- A plan coordinator owns one constraint ledger. Every plan resolves a complete claim set for memory-domain and host effects, per-type count slots, compatibility slots, adapter-declared constraints, and the full ordered action-lease set. It acquires all claims atomically in deterministic order before the first side effect; all competing plans and acquisitions contend with the ledger, and the reservation remains until claims transfer to a terminal occupancy or release state. Admission, measured pressure, NPU conflict, startup loading, and load-failure retry all use this boundary and abandon stale plans before mutation.
- Reconcile every incoming load before advancing or committing its plan. Verify the resolved backend, device, topology, liveness, actual per-domain and host effects, count and compatibility occupancy, and required signal freshness; then atomically convert the matching reservation into provisional occupancy. Excess use, ambiguity, or identity/topology deviation aborts the plan. Never enlarge a reservation opportunistically after side effects begin.
- Reclamation side effects remain fallible even when the decision is atomic. Execute victims one at a time; on the first failure, stop the remaining plan, do not start the incoming load, preserve untouched residents, report the successful and failed actions, and do not automatically reload already reclaimed models.
- Reconcile every action before crediting recovered capacity. Verified release transitions to `unloaded`; a verified-intact failure returns to the appropriate resident state; an ambiguous or unreachable backend transitions to `quarantined`. Quarantine is non-routable and inherits the maximum plausible memory and host effects plus every count, compatibility, and adapter-declared claim until cleanup verifies release. A plan reservation ends only through an atomic transfer to terminal occupancy or release, and admission never relies on capacity whose release is unverified.
- If admission cannot create a safe plan, or measured pressure remains after all eligible actions, refuse or stop and report the unresolved condition rather than reclaiming a protected model or making arbitrary extra changes.
- `lemond` owns the operational explanation contract. Structured API state and logs use one stable plan id and report the trigger, initiating request or client when applicable, starting resident state and signal generations, per-operation capability and effective mode, constraints, incoming demand and footprint confidence, ordered proposed and actual actions, per-action outcomes, refusals, fallbacks, partial outcomes, terminal outcome, and reason codes. Startup plans also report set membership, resolved-intent and configuration generations, transaction phase, provisional members, rollback actions, and claims retained by suspension or quarantine. Recovery state reports the barrier phase, owner-lease status, blocked lifecycle scope, record identity and validity without secrets, retained claims, cleanup outcome, and readiness transition; clients only render that server-owned truth.
- Public server configuration stays small and intent-oriented. Sensor selection, calibration details, victim-planning internals, and similar mechanisms are derived unless an operator-facing need is demonstrated.
- These behavioral guarantees are fixed for the portable specification; sensor sources, topology discovery, footprint estimation, thresholds, hysteresis, polling, cold/warm classification, adapter shapes, public names, and concrete validation scenarios remain implementation-design work.
- The implementation handoff must include a platform/backend matrix with per-operation and affected-domain-set capability cells, primary evidence, backend recovery authority, effective fallback or refusal behavior, and the tests required to advance each cell. Validation includes concurrent-daemon owner exclusion; crash injection before spawn, immediately after process creation but before launcher return or record update, during provisional loading and commit, during rollback, and after committed residency; plus complete descendant cleanup, PID reuse, truncated or malformed records, claim replay, and readiness gating for every supported subprocess-ownership model.

## Frontend Split

- `src/app/src/renderer/` is the shared React renderer.
- The Tauri desktop app uses `src/app/src-tauri/` and installs `window.api` through `tauriShim.ts`.
- The browser web app in `src/web-app/` reuses the shared renderer with a separate package/dependency tree.
- Do not consolidate `src/app/package.json` and `src/web-app/package.json`; the split supports Debian native packaging with system Node modules.

### Accepted residency UI target

- Runtime pin state, saved pin preference, and startup-loading policy use distinct controls and labels. A pin action never silently changes the startup policy.
- Saved pin preferences and startup failures remain visible when their models are not loaded so users can retry, change options, or remove the preference.
- The startup-loading setting is server-owned lifecycle configuration. It must not move into per-client desktop or web-app settings.
- Residency rows and persistent recovery messages surface loaded, in-use, pinned, remembered-pin, startup-provisional, committing, rolling-back, suspended, cleanup-required/quarantined, soft-reclaimed, pressure-reclaimed, and refusal states with text or accessible names in addition to color. Provisional members are never presented as loaded or pinned.
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
