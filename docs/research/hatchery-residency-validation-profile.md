# Hatchery residency validation profile

Status: accepted Issue [#35](https://github.com/nisavid/lemonade/issues/35) validation contract; not proof of current implementation or physical validation.

Date: 2026-08-12

## Accepted Hatchery contract

Use Hatchery as the first narrowly validated capability profile, with separate capability cells for predictive admission, measured-pressure reclamation, grouped startup admission, and lifecycle recovery. Validate Hatchery `llamacpp:rocm` first, then qualify `llamacpp:vulkan` as a separate cell without making it block the first usable release. Keep every untested backend/operation cell at `modeled`, `fallback-only`, or `unsupported`; do not infer a combination-wide `validated` label.

The first physical gate combines:

1. Actual downloaded models spanning tiny through near-capacity footprints.
2. Prospective high-context, multi-model, and near-boundary workloads.
3. Deterministic synthetic adapters and pressure generators for conditions that real models cannot reproduce reliably.
4. Real external GPU and host-memory pressure from processes that Lemonade does not own.
5. Barrier-controlled concurrency and crash injection, not timing-dependent race tests.

No capability cell advances to `validated` unless its complete promotion-evidence index and every referenced immutable run and evidence bundle pass each mandatory row for that operation, backend, affected domain set, and runtime fingerprint.

## Accepted contract used as the oracle

This profile applies the accepted decisions rather than redefining them:

- The portable adapter, topology, recovery, and claim contract is in [Issue #31](https://github.com/nisavid/lemonade/issues/31#issuecomment-5262029591).
- Analytic-first footprint prediction, confidence classes, external-demand accounting, and unknown-demand refusal are in [Issue #32](https://github.com/nisavid/lemonade/issues/32#issuecomment-5262927746).
- Pressure phases, signal freshness, victim eligibility, useful-recovery scoring, partial outcomes, and scheduling are in [Issue #33](https://github.com/nisavid/lemonade/issues/33#issuecomment-5264397496).
- Public configuration, explanation records, effective modes, and reason-code conformance are in [Issue #34](https://github.com/nisavid/lemonade/issues/34#issuecomment-5274903942).
- The concise architectural target is in [architecture-map.md](../agents/architecture-map.md), especially its accepted model-residency section. The domain terms are in [CONTEXT.md](../../CONTEXT.md).

The resulting test oracle is:

- Hatchery has one model-residency domain: GTT/shared GPU memory.
- Host `MemAvailable` is an independent system-health constraint over the same physical RAM, not another model-footprint pool.
- Predictive admission must reserve a complete conservative lifetime envelope before any eviction or spawn.
- Automatic hard reclamation may target only unpinned, idle residents. An in-use or runtime-pinned resident is a veto, including under critical pressure.
- External demand reduces available capacity but never becomes Lemonade-owned state or a cleanup target.
- Missing, stale, incoherent, or unhealthy evidence cannot authorize automatic action or prove recovery.
- Every observable result must agree with the server-owned explanation and reason registry.

## Primary evidence

### Platform semantics

AMD documents Strix Halo as physically shared CPU/GPU memory. GTT is the amount of system RAM that user processes may map into GPU virtual address spaces; allocations are dynamic rather than a separate permanently reserved physical pool. AMD recommends a small firmware VRAM reservation and a larger TTM/GTT limit for AI workloads. See [AMD Strix Halo system optimization](https://rocm.docs.amd.com/en/docs-7.2.0/how-to/system-optimization/strixhalo.html#memory-settings).

The Linux amdgpu ABI exposes `mem_info_gtt_total` and `mem_info_gtt_used` in bytes as the global GTT capacity and current use. It separately exposes VRAM counters. See [AMDGPU memory usage information](https://docs.kernel.org/gpu/amdgpu/driver-misc.html#gpu-memory-usage-information).

Linux defines `MemAvailable` as an estimate of memory available for starting new applications without swapping, accounting for reclaimable caches, slab, and zone watermarks. See [the `/proc/meminfo` documentation](https://docs.kernel.org/filesystems/proc.html#meminfo).

Linux PSI reports system and cgroup memory-stall time. PSI is useful corroborating safety evidence, but this contract does not make it an admission authority until a catalog rule defines its semantics and effect model. See [Pressure Stall Information](https://docs.kernel.org/accounting/psi.html).

DRM fdinfo may expose per-client memory-region statistics. Where amdgpu provides complete, stable keys on Hatchery, they are useful attribution evidence; global sysfs remains the physical-domain observation. See [DRM client usage stats](https://docs.kernel.org/gpu/drm-usage-stats.html).

### Current upstream implementation

At upstream commit [`3e532596`](https://github.com/lemonade-sdk/lemonade/commit/3e53259600bf9076d51fd9dee889732e0df0c11a), `SystemInfo::get_global_vram_usage_pct()` reads amdgpu VRAM and GTT sysfs counters, treats an APU as a combined VRAM+GTT Vulkan budget, and also derives a stricter preferred-pool ratio for ROCm. See [`system_info.cpp`](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/system_info.cpp#L4224-L4315). The current pressure monitor polls this ratio and the eviction engine reacts to a global threshold; it does not implement the accepted Hatchery GTT-plus-host-floor predictive contract. See [`global_vram_monitor.cpp`](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/global_vram_monitor.cpp) and [`eviction_engine.cpp`](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/eviction_engine.cpp).

This source is evidence about upstream behavior, not a validation oracle for the fork target.

## Hatchery fingerprint and baseline

The following live, read-only snapshot was captured on 2026-08-12. Record these fields again for every validation run; this snapshot must not authorize later runs by itself.

The snapshot used `uname`, `lscpu`, `lspci`, `/proc/meminfo`, `/proc/pressure/memory`, `/sys/class/drm/card1/device/mem_info_*`, `/sys/module/ttm/parameters/pages_limit`, `amd-smi version`, `amd-smi metric --mem-usage`, `lemonade status`, and `lemonade list`. The run harness preserves the raw outputs rather than only this summary.

| Field | Observed value |
| --- | --- |
| Host | `hatchery` |
| CPU/APU | AMD Ryzen AI MAX+ 395 with Radeon 8060S |
| GPU PCI identity | `0000:c6:00.0`, AMD `1002:1586`, revision `c1` |
| GPU architecture | Strix Halo / expected `gfx1151`; runtime attestation still required |
| Kernel | `7.1.6-1-cachyos` |
| Host usable RAM | `133,617,377,280` bytes, about 124.44 GiB |
| TTM page limit | `29,360,128` pages |
| `mem_info_gtt_total` | `120,259,084,288` bytes, exactly 112 GiB |
| `mem_info_gtt_used` | `1,870,688,256` bytes at capture time |
| `mem_info_vram_total` | `1,073,741,824` bytes, exactly 1 GiB |
| `mem_info_vram_used` | `893,116,416` bytes at capture time |
| `MemAvailable` | `115,222,822,912` bytes at capture time |
| Swap | 32 GiB configured; approximately 5.6 GiB used at capture time |
| AMD SMI / ROCm | AMD SMI 26.4.0, ROCm 7.13.0 |
| Installed Lemonade | 11.5.2; no model resident at capture time; `max_loaded_models=-1` |

The 1 GiB VRAM counter is not the Hatchery model-capacity budget. The hypothesis to validate is that model weights, fixed cache, and retained KV cache for each approved backend/workload are placed only in GTT. Every physical workload must prove that hypothesis with coherent global counter deltas and, where available, backend-process DRM fdinfo. A persistent model-attributable VRAM delta or unclassified placement blocks promotion until the topology and predictor are corrected.

## Capability-cell identity and promotion

A cell is keyed by all facts that can change permission or measurement:

- platform and durable hardware identity;
- OS, kernel, amdgpu/ROCm/Vulkan runtime, backend executable, and Lemonade build identities;
- recipe, backend, device, topology generation, and affected domain set;
- runtime operation template and leaf (`ADM` / `admission`, `PRE` / `pressure_reclamation`, `STA` / `startup_load`, or `REC` / `prior_epoch_owner_cleanup`);
- pressure profile, configuration, catalog, predictor, and recovery-authority identities;
- model architecture/encoding applicability and request/workload class.

An evidence-only compatibility contract has a separate identity: the frozen source baseline, exact source-derived participant cases, directions, incumbent states, model-type coverage, `NPC` relation selector, enumerated relation constraints, suite and gate sets, evidence ceiling, capability and delivery state, and concrete fallback. It never borrows a runtime cell's hardware, process, claim, or recovery identity. Its gates can prove only the enumerated relation constraints; `delivery_state=absent` leaves the contract unavailable regardless of planned or collected synthetic evidence.

Promotion rules:

| Level | Minimum evidence |
| --- | --- |
| `unsupported` | Primary evidence or physical failure shows that no safe automatic or declared fallback behavior exists. The server refuses with the registered reason and makes no side effect. |
| `fallback-only` | The declared conservative fallback/refusal is fully specified and passes conformance, protection, recovery, and explanation tests. No capacity automation is implied. |
| `modeled` | Primary architecture evidence, complete normalized contracts, every applicable analytic component bound, deterministic synthetic tests, and the cataloged concrete fallback/refusal pass, but the full physical matrix has not. Analytic memory bounds are mandatory only for resource-capacity cells. |
| `validated` | The exact cell passes the mandatory physical, concurrency, fault, restart, and conformance rows below with complete immutable evidence. For footprint authority, held-out physical workloads remain under the reviewed conservative bound. |

Promotion is monotone only within one immutable catalog revision. Any bound exceedance, newly unclassified placement, failed recovery invariant, or material fingerprint change invalidates the matching runtime cell and requires a future reviewed catalog revision; it does not expand a live bound.

## Representative workload portfolio

Downloaded artifact size selects test diversity but is never used as the predicted resident footprint.

### Actual downloaded models

Use at least one model from every applicable row, with exact artifact hashes, recipe options, context settings, and backend identity captured in the run manifest.

| Class | Current Hatchery candidate | Why it is included |
| --- | --- | --- |
| Tiny control | `Tiny-Test-Model-GGUF` (0.17 GB downloaded) | Fast lifecycle, concurrency, rollback, and leak control. |
| Small dense LLM | `Qwen3.5-4B-MTP-GGUF-UD-Q4_K_XL` (3.41 GB) | Common load/inference/KV-growth path. |
| Alternate small LLM | `Qwen3.5-4B-heretic-v2-i1-GGUF-Q4_K_M` (2.52 GB) | Held-out encoding/metadata variation. |
| Medium/large LLM | One of the downloaded 17.0–22.1 GB GGUF models | Meaningful GTT delta, multi-resident planning, and hard reclamation. |
| Near-boundary LLM | `trohrbaugh-Qwen3.5-122B-A10B-heretic-i1-GGUF-Q4_K_M` (69.1 GB) | Large single-resident envelope and near-capacity combinations. |
| vLLM probe | `Qwen3.5-0.8B-FP16-vLLM` (3.3 GB) | Separate backend/process/allocation behavior; it remains a separate capability cell. |
| Auxiliary model types | downloaded embedding and reranking GGUF models | Per-type concurrency, cross-type residency, and false count/compatibility assumptions. |

### Prospective workloads

These are workload shapes, not promises about a particular catalog model:

- A high-context request that reaches the cataloged maximum retained KV alternative.
- Multiple simultaneous sequences that reach the maximum transient-plus-retained envelope.
- A mixed resident set whose total artifact size looks safe but whose complete predicted lifetime envelopes approach the GTT or host-floor boundary.
- A grouped startup set containing at least three saved pin preferences whose joint envelope is feasible, and a second set whose joint envelope is infeasible.
- A model/runtime fingerprint not included in predictor applicability, to prove refusal before reclamation or spawn.
- A model whose complete envelope fits GTT but would breach the host-memory floor, and the converse case.

### Synthetic fixtures

Use a deterministic fake backend/provider only for exact state and planner coverage:

- exact persistent, transient, retained, and shared-allocation effects;
- bounded, incomplete, and unknown footprint manifests;
- intentional predictor exceedance;
- delayed, failed, verified-intact, ambiguous, and successful actions;
- stale, superseded, malformed, and incoherent observation sets;
- reusable PID values with distinct process-start identities;
- crash barriers at every durable transition.

Synthetic success can establish conformance or `modeled`, never physical `validated` status.

## Controlled external pressure

Run pressure tests on an otherwise quiescent Hatchery validation session with an out-of-band watchdog and a predeclared stop boundary. Never use uncontrolled whole-system exhaustion.

### External GTT pressure

Use a standalone HIP/ROCm allocation helper, outside Lemonade's process tree and recovery authority. Allocate in bounded steps, touch and read back every allocation so lazy reservation cannot masquerade as use, and hold each step until a coherent observation generation records it. Confirm each step increases global `mem_info_gtt_used` within the measurement envelope. Where available, correlate the helper's DRM fdinfo without converting it into Lemonade ownership.

After the deterministic helper passes, repeat one pressure row with a real independent GPU workload, such as a non-Lemonade ROCm inference process. Lemonade must reduce its own eligible residency if policy calls for it, while leaving the external process alive and its allocation intact.

### External host-memory pressure

Use a separately owned anonymous-memory helper in a dedicated cgroup. Fault pages in bounded steps and hold them, proving that `MemAvailable` falls while GTT use remains within the no-change noise envelope. Stop before the predeclared host guard. Cgroup `memory.events`, system PSI, swap counters, and the watchdog are safety evidence, not substitutes for the host-floor controller signal.

### Combined pressure and release

Run one GTT allocator and one host allocator concurrently, then release them in both orders. This proves that:

- one Lemonade GTT-backed allocation can improve both constraint margins when reclaimed without being counted twice as physical memory;
- external demand is never inserted into the Lemonade claim ledger;
- withdrawing external pressure before hard-action dwell prevents an unnecessary hard action;
- recovery exit requires fresh coherent evidence and the configured stabilization interval.

## Accepted Hatchery validation matrix

`L` means the real Hatchery `llamacpp:rocm` runtime fixture and environment; each row is assigned only to the applicable operation-specific promotion cell through the machine gate registry. `V` means the separate vLLM ROCm fixture. `S` means deterministic synthetic support. Exact backend names and versions must be frozen before execution.

| ID | Fixture | Applications | Trigger | Required result |
| --- | --- | --- | --- | --- |
| H-TOP-01 | L, V | exact_runtime | Cold baseline and each actual model class | Attest one GTT residency domain plus host floor; classify every model effect; no unexplained model-attributable VRAM placement. |
| H-FP-01 | L | exact_runtime | Actual portfolio, high context, and concurrent sequences | Every phase's observed effect stays within its analytic component/lifetime bound plus declared uncertainty; validate residual on held-out models. |
| H-ADM-01 | L | exact_runtime | Incoming model safely fits | Load without reclamation; reservation precedes spawn; observed placement narrows but never enlarges claims. |
| H-ADM-02 | L | exact_runtime | Incoming model fits only after reclamation | Reserve the complete deterministic eligible set before action, reclaim only enough, reconcile, then load without exceeding either constraint. |
| H-ADM-03 | L, S | exact_runtime, exact_synthetic | Unknown/incomplete incoming footprint | Refuse before any resident reclamation or backend spawn with the registered confidence/evidence reason. |
| H-ADM-04 | L | exact_runtime | GTT-fit/host-floor-fail and host-fit/GTT-fail cases | Refuse or reclaim against the deficient constraint vector; do not alias, sum, or ignore either constraint. |
| H-GROW-01 | L | exact_runtime | KV/transient/retained growth at maximum context/concurrency | Reserve the complete lifetime alternative before effect, or refuse before growth; unprepared retention is an envelope violation and quarantine path. |
| H-PRE-01 | L | exact_runtime | External GTT steps cross reclaim entry | Observe external demand, honor dwell, use soft-first strata, execute at most one hard action per pressure plan, and reach stabilized recovery without touching the external process. |
| H-PRE-02 | L | exact_runtime | Host-only allocator crosses host-floor entry | Act on the host-floor deficit even while GTT remains normal; verified release must improve the expected constraint effects. |
| H-PRE-03 | L | exact_runtime | Combined GTT and host pressure | Score the full deficit vector; count shared allocation bytes once; persist the plan key and first-action comparison evidence. |
| H-PRE-04 | L | exact_runtime | Pressure withdrawn before dwell, during recovery, and after renewed pressure | No premature hard action; correct phase, dwell-reset, and stabilization behavior from monotonic time. |
| H-PROT-01 | L | exact_runtime | Only pinned residents could solve pressure/admission | Do not hard-reclaim them; refuse or report unresolved pressure with residency unchanged. Idle pinned soft reclamation is allowed only if its action preserves weights/process/pin. |
| H-PROT-02 | L | exact_runtime | Candidate actively serving a long request | Preserve the request and resident; expose bounded `waiting_for_in_use`; act only after the final use lease releases and a fresh plan selects it. |
| H-ORD-01 | L, S | exact_runtime, exact_synthetic | Cold and warm residents with tied and untied useful effects | Follow fixed strata and deterministic tie keys, not artifact size, load cost, registry order, or current request recency. |
| H-CON-01 | L, S | exact_runtime, exact_synthetic | Two individually feasible admissions whose combination is infeasible | Exactly one complete reservation wins; the other retries/refuses without oversubscription or partial unjournaled effects. |
| H-CON-02 | L, S | exact_runtime, exact_synthetic | Pressure planning races load, inference, pin, unload, and signal refresh | One ledger/gate serializes conflicts; disjoint work proceeds; stale plan tokens cannot commit. |
| H-EVD-01 | S plus live signal fault | exact_runtime, exact_synthetic, compatibility_synthetic | Signal missing, stale, superseded, unhealthy, malformed, or topology-changed | Select the declared concrete fallback/refusal; never initiate action, clear an active episode, or publish success from invalid evidence. |
| H-STA-01 | L | exact_runtime | Feasible and infeasible grouped saved-pin startup sets | Feasible set publishes atomically with runtime pins; infeasible set loads none and preserves preferences; independent sets still proceed. |
| H-NPU-01 | S NPU compatibility relation fixture | compatibility_synthetic | Every source-derived platform case and incoming direction with unpinned-idle, pinned, and in-use incumbents | Classify the `npu_cross_family` conflict, preserve every incumbent, and refuse the incoming load before eviction or backend spawn; unpinned-idle state grants no displacement authority. |
| H-REC-01 | L, S | exact_runtime, exact_synthetic | Crash/restart matrix and second-daemon attempt | No adoption of survivors; complete claim replay; only proven-owned cleanup; lifecycle remains fenced until verified release. |
| H-EXP-01 | all | exact_runtime, exact_synthetic, compatibility_synthetic | Every terminal and degraded path applicable to the selected promotion unit | API, compact status, structured log, and compatibility response agree on operation, phase, outcome, effective mode, ordered reasons, and retention metadata. |

The machine inventory's exact roster independently promotes Hatchery `llamacpp:rocm` admission, pressure reclamation, prior-epoch recovery, and—after grouped startup exists—startup-load cells. Each consumes only its fully expanded atomic gate set; no profile-wide result promotes the other operations. Vulkan and vLLM remain separate runtime cells, while NPU/FLM compatibility remains a separate evidence-only promotion contract until exact platform runtime cells exist; none inherits a Hatchery ROCm result.

## Exact pass/fail evidence contract

### Frozen run manifest

Before a test mutates residency, persist an immutable run manifest containing:

- test ID, attempt ID, seed, monotonic start, and operator/test-harness build;
- hardware, firmware where available, PCI identity, kernel, driver/runtime, backend executable, Lemonade binary, and source commit hashes;
- configuration authority root and effective values, including `max_loaded_models=-1`, `memory_pressure_reclamation=automatic`, optional `residency_memory_limit_gib`, and startup policy;
- topology, catalog, capability, predictor, pressure-profile, recovery-mode, and reason-registry digests;
- model artifact hashes, recipe options, backend/device resolution, prompt/request shape, context, batch, sequence/concurrency, and cache policy;
- predeclared GTT and host controller thresholds, dwell/stabilization durations, freshness/skew limits, sampling uncertainty, watchdog limit, and measurement noise envelope;
- the expected operation result and allowed registered reason codes.

Changing any field creates a new attempt and, where material, a new capability cell. Thresholds and tolerances cannot be fitted after seeing the run.

### Evidence stream

Capture on one monotonic timeline:

- raw GTT/VRAM totals and use, `MemAvailable`, swap counters, memory PSI, and cgroup `memory.events`;
- provider source generations, observation-set generations, health, freshness, skew, uncertainty, and topology/configuration generations;
- backend-process tree and recovery identities, DRM fdinfo when supported, and external-helper identities;
- every reservation, complete claim set, action lease, planner candidate/set key, action, reconciliation, claim transfer, quarantine, and lifecycle-gate transition;
- the exact canonical explanation revisions and all API/log/compatibility projections;
- request start/end/cancel/failure and resident-use lease acquisition/release;
- watchdog heartbeat and OOM-kill evidence.

### Universal failure conditions

One occurrence fails the attempt:

- an automatic hard action targets a pinned or actively used resident;
- any eviction or backend spawn occurs before a complete sufficiently confident reservation;
- observed effects exceed the prepared envelope, appear in an unclassified memory region, or are double-counted;
- GTT or host-floor safety is breached by Lemonade's admitted effect beyond the declared observation uncertainty;
- external demand becomes a Lemonade claim, is signaled, or is cleaned up;
- a claim is released without verified physical/process release;
- invalid/stale evidence initiates action or proves recovery;
- a stale token commits, two conflicting reservations coexist, or an explanation disagrees with authority state;
- the kernel or cgroup reports a new OOM kill, the watchdog misses its predeclared bound, or an unrelated process dies;
- a test finishes with unaccounted allocations, processes, leases, claims, or fences. Expected injected ambiguity must instead finish in the exact quarantine/recovery-required state declared by that test.

### Operation-specific numerical oracles

For every sample, use canonical integer bytes and declared uncertainty. Let `G` be effective GTT capacity (the smaller of the attested GTT total and an applicable configured limit), `U` global GTT use, `R` only reservations not already included in `U`, `A` host `MemAvailable`, and `F` the host floor.

- GTT admission margin is `G - U - R`; host margin is `A - F` after applying the candidate's conservative host effect. Observation coverage must prove whether a Lemonade claim is already included in `U`; it is never blindly added twice.
- A predicted bound passes only when every reachable simultaneously-live observed component set, including retained alternatives, is less than or equal to its bound after measurement uncertainty. Calibration workloads and held-out validation workloads are disjoint.
- A pre-spawn refusal test passes only with zero automatic victim actions, zero backend spawn, unchanged residency/claims except the terminal operation record, and the exact registered reason.
- A protection test passes only when the selected hard-action set contains no resident whose use count is nonzero or runtime pin is true at action-lease acquisition and revalidation.
- A pressure test passes only when phase transitions use the frozen monotonic predicates, no hard action begins before its dwell unless `critical` is freshly proven, each hard plan executes only its deterministic first action, and recovery exits only after the frozen stabilization interval on fresh coherent observations.
- A release passes only when post-action evidence proves the process/resource tree is gone and the observed constraint effect is consistent with the action's conservative effect envelope. Ambiguity must retain maximum plausible claims.

### Repetition and held-out evidence

- Run each physical matrix row in at least three independent cold-daemon attempts from a reconciled baseline.
- Exercise each barrier-controlled concurrency interleaving and fault point three times. Do not rely on probabilistic sleeps; the harness must stop at named transition barriers.
- Run deterministic planner/property fixtures across a fixed published seed corpus, including boundary values and checked-arithmetic overflow.
- Derive the residual envelope from a declared calibration subset and assess promotion on a held-out subset. A model used to enlarge the residual is not held-out evidence for that revision.
- Store every failure. Excluding an attempt requires a predeclared invalidation rule, such as unrelated demand changing beyond the allowed baseline envelope; it cannot be discarded because the outcome is inconvenient.

## Concurrency and failure-injection schedule

Mandatory concurrency barriers:

1. Two admissions after planning but before ledger reservation.
2. Admission after victim reservation but before the first action.
3. Inference and runtime-pin acquisition while a soft or hard action lease is pending.
4. Pressure refresh while a newer native event arrives.
5. Configuration/catalog/topology transition before each irreversible action and terminal commit.
6. Explicit unload or force operation while an automatic plan holds a conflicting lease.
7. A second `lemond` attempting the same recovery store while the first owner is alive.

Mandatory crash points:

1. Before backend spawn.
2. Immediately after process creation but before launcher return or record update.
3. During provisional loading.
4. After observed-effect reconciliation but before commit.
5. During grouped commit.
6. During rollback and verified release.
7. After committed residency.
8. During soft or hard reclamation before backend acknowledgement, after acknowledgement, and before atomic plan closure.
9. During quarantine or artifact/recovery record publication.

For each crash point, restart must replay maximum claims, prove the prior daemon dead or fenced, avoid PID-only ownership, reject a deliberately reused PID with a different start identity, never adopt survivors, clean only proven-owned descendants, and keep ordinary lifecycle unavailable until the journal and ledger agree. Repeat with truncated, malformed, missing-content, and older-schema records. A second live daemon must not classify the first daemon's children as orphans.

## Resolved Issue 35 decisions

The versioned [Hatchery campaign parameters](hatchery-campaign-parameters.md) resolve the initial backend, pressure and recovery thresholds, host floor, PSI role, sampling and uncertainty bounds, workload partition, helper design, cold-attempt schedule, fingerprint invalidation, NPU/FLM ceiling, evidence distribution, and privacy policy. [Profile-free residency estimation](profile-free-residency-estimation.md) defines the accepted narrow estimator rule, while [Upstream validation-data conventions](upstream-validation-data-conventions.md) defines catalog and evidence distribution.

This contract is executable only after it is flattened into the content-addressed campaign manifest defined by the overlay and all pre-collection deployment bindings are present. Acceptance of the design does not promote any cell or prove implementation.
