# Hatchery residency campaign parameters

Status: research proposal for Issue [#35](https://github.com/nisavid/lemonade/issues/35), not accepted policy, an implementation plan, or validation evidence.

Date: 2026-08-13

## Scope and precedence

This document is a versioned research overlay on [Hatchery residency validation profile](hatchery-residency-validation-profile.md) at commit `13a5c6aa`. It narrows the first physical capability profile to Hatchery `gfx1151`, `llamacpp:rocm`, the exact attested backend/dependency fingerprint, and the closed text-only v1 predictor predicate from [Profile-free residency estimation](profile-free-residency-estimation.md). Within that profile, predictive admission, measured-pressure reclamation, grouped startup admission, and lifecycle recovery are separate promotion cells keyed by exact operation and affected-domain set; the shared Hatchery constraint vector is GTT plus the independent host-`MemAvailable` floor. NPU/FLM conflict is the separate cell `H-NPU-FLM-CONFLICT-XDNA2-v1`, with its own device/compatibility domains and evidence ceiling. This overlay supersedes only the base profile's unspecified initial backend, workload portfolio, numeric pressure/timing/sampling profile, cold-attempt minimum, NPU evidence status, and evidence-storage proposal.

The executable campaign must flatten the base plus this overlay into one content-addressed immutable campaign-contract manifest. Every value and exact workload must resolve in that contract; no runtime or reviewer may infer precedence between prose documents. A threshold, predicate, workload, deadline, or evidence-location change creates a new campaign-contract revision even when the change appears more conservative.

Use an acyclic append-only evidence DAG:

1. Before execution, the campaign contract references frozen schemas, attempt IDs, the content-addressed predictor-rule revision, and the precomputed complete predicted residency-effect manifests produced by that rule.
2. Each later per-attempt run manifest references the campaign-contract digest.
3. Each evidence-bundle manifest references its run manifest.
4. A later promotion-evidence index references the completed contract, runs, bundles, reviews, acceptance, and catalog candidate.

No earlier immutable object acquires a forward reference or changes digest after execution. These artifact types have independent typed digests and revision lifecycles.

This first Hatchery profile does not complete Issue #35. Separate Wayfinder-charted follow-up routes must define the conceptual platform × backend × operation × affected-domain matrix and evidence plan for every supported residency-relevant backend family—including GPU, NPU/FLM, and host-only cells—then prioritize cross-platform and cross-backend physical cells, multi-device attribution, signal-loss/staleness, unknown-footprint, concurrency, crash/PID-reuse, quarantine, rollback, and lifecycle-readiness scenarios. No profile-wide summary label grants authority to any constituent cell.

### Exact first-profile cell roster

The first profile contains exactly these independently promoted cells. Every row also requires the common attested Hatchery/ROCm dependency closure, topology and signal qualification in `H-TOP-01`, evidence-authority conformance in overlay row `H-LIV-01`, the applicable exact v1 workload predicate, and explanation conformance in `H-EXP-01` from the base profile:

| Cell ID | Operation and affected domain set | Additional mandatory rows | Promotion ceiling | Stable fallback rule(s) |
| --- | --- | --- | --- | --- |
| `H-ROCM-ADM-GTT-HOST-v1` | Predictive admission and lifetime growth over canonical GTT headroom plus the host-`MemAvailable` floor | `H-FP-01`, `H-ADM-01..04`, `H-GROW-01`, `H-PROT-01..02`, `H-ORD-01`, `H-CON-01..02`, `H-EVD-01`, `H-REC-01` | `validated`, with a separately reviewed catalog assigning `validated_predictor` to the complete residency-effect manifest produced by the referenced predictor rule | `hatchery_rocm_admission_refuse_unknown_capacity_v1`: refuse capacity-dependent admission before resident reclamation or backend spawn; the independent count ceiling makes no memory-safety claim. |
| `H-ROCM-PRE-GTT-HOST-v1` | Measured-pressure reclamation over the same two byte constraints | `H-FP-01`, `H-PRE-01..04`, overlay row `H-EXT-01`, `H-PROT-01..02`, `H-ORD-01`, `H-CON-02`, `H-EVD-01`, `H-REC-01` | `validated` | With valid reporting evidence but no action authority, `hatchery_rocm_pressure_report_only_v1`; with missing, stale, unhealthy, or incoherent reporting evidence, `hatchery_rocm_pressure_disabled_invalid_evidence_v1`. Both preserve residency, perform no automatic soft or hard mutation, and cannot clear an active episode until fresh coherent recovery evidence returns. |
| `H-ROCM-STA-GTT-HOST-v1` | Grouped startup admission over the same two byte constraints | `H-FP-01`, `H-STA-01`, `H-CON-01..02`, `H-EVD-01`, `H-REC-01` | `validated`, separately and only after grouped startup exists | `hatchery_rocm_startup_block_group_v1`: block the startup group, load none of its members, and preserve every saved preference. |
| `H-ROCM-REC-GTT-HOST-OWN-v1` | Lifecycle recovery over GTT, host-memory, and Lemonade-owned process/claim closure | `H-EVD-01`, `H-CON-02`, `H-REC-01` | `validated` | `hatchery_rocm_recovery_block_readiness_v1`: keep lifecycle readiness blocked and retain maximum plausible claims; perform no adoption or automatic cleanup unless a separately matched cleanup mechanism proves ownership and release. |

The row list is conjunctive, not illustrative. Shared evidence may be referenced by several cells, but a pass for one operation or affected-domain set cannot promote another.

`H-LIV-01` is an executable suite and owns the one closed `LivenessRevalidationBoundary` set: `plan_creation`, `plan_or_action_reservation`, `capacity_affecting_effect_dispatch`, `incoming_load_commit`, and `terminal_claim_transfer`. `capacity_affecting_effect_dispatch` covers backend spawn/load, request transient allocation, KV/retained growth, and every automatic soft or hard action. Its frozen actor set is the daemon, local authority journal, authorized issuer, independent witness, primary/backup stores, and a fault injector. `H-LIV-01a` proves success/renewal and per-cell conservative meet; `01b` covers expiry and copy/digest/size/root failure; `01c` injects bad signature, equal-sequence competing digest, non-tip predecessor, fork, old-head replay, and witness/local rollback; `01d` injects local-head, witness, and partial publication failure; `01e` covers witness outage/delay, renewal-expiry race, clock rollback/uncertainty, suspend/resume, and daemon/boot restart; `01f` injects expiry or cap change immediately before every member of `LivenessRevalidationBoundary`; and `01g` proves fallback and restoration only after a fresh tip/checkpoint/lease. At every `01f` barrier, a no-effect operation stops before the effect; an existing provisional effect is canceled and rolled back under its owner or transferred atomically to a quarantined recovery disposition, with maximum plausible claims retained until verified; and an already committed effect takes the partial-outcome/recovery path with maximum claims retained. Every subrow uses synthetic fault barriers plus three daemon-cold repetitions; rollback, restart, suspend/resume, and cross-boot time cases also run once in each boot-cold epoch. Pass evidence includes signed chain/lease/checkpoint bytes, before/after authority generations and plan tokens, structured reasons/effective modes, zero forbidden side effects, and retained claims/recovery disposition for any effect already committed.

`H-EXT-01` freezes before execution the campaign-owned external cgroup, PyTorch source/package/environment lock, interpreter and native-library hashes, ROCm/driver closure, script digest, device, allocation/inference shape, maximum rate/cap, seed, and request trace. The process remains external to Lemonade authority but joins `CampaignControlledActorSet` for campaign cleanup. In each required runtime-cold repetition, it crosses the applicable pressure entry while Lemonade has an eligible resident, then holds and releases demand. Pass requires identity-bound GTT/host/cgroup/DRM evidence that the allocation remains outside Lemonade's process tree, ledger, claims, and recovery authority; only the eligible Lemonade resident may be selected; the external PID/process-start identity survives until campaign release; global and identity-scoped deltas reconcile; and recovery stabilizes after external release. Any ownership/termination/claim leak fails the pressure cell.

## Recommendation

Freeze a Hatchery-only experiment profile before collecting promotion evidence, but do not mislabel its initial constants as portable safety facts. The campaign should distinguish three kinds of boundary:

1. A **safety predicate** is derived from a conservative effect envelope, observation uncertainty, and the bounded time needed to stop or release controlled work. Crossing it means the validated assumptions no longer prove safety.
2. A **utility threshold** expresses how much headroom the operator wants to preserve before ordinary pressure reclamation. It is a policy choice, not a property of Strix Halo or Linux.
3. An **initial experiment parameter** is a deliberately conservative starting value used to gather the data that can justify or revise the first two. It cannot leave the campaign as an undocumented magic constant.

For the first campaign, use absolute byte thresholds rather than a generic percentage of GTT or RAM. Use a small native HIP pressure helper, make PSI corroborating rather than capacity-authoritative, and keep NPU/FLM physical validation out of the initial promotion gate because Hatchery's NPU is not currently usable. Store small sanitized evidence in Git and large sanitized bundles as content-addressed GitHub Release assets; use Actions artifacts only as transit.

## Primary-source constraints

### One shared physical memory, two constraints

AMD documents Strix Halo GTT as dynamically mapped system RAM rather than a separate physical pool. `mem_info_gtt_total` is a mapping limit, while host availability still constrains the same physical RAM. AMD also says AI frameworks normally prefer GTT-backed allocations on this platform. See [AMD's Strix Halo memory settings](https://rocm.docs.amd.com/en/docs-7.2.0/how-to/system-optimization/strixhalo.html#memory-settings).

The amdgpu ABI defines `mem_info_gtt_total` and `mem_info_gtt_used` as the global GTT block size and current used amount, in bytes. It does not prescribe a safe usage percentage or a reclamation threshold. See [AMDGPU memory usage information](https://docs.kernel.org/gpu/amdgpu/driver-misc.html#gpu-memory-usage-information).

Linux defines `MemAvailable` as an estimate of memory available for starting new applications without swapping. It accounts for free memory, reclaimable cache and slab, and zone watermarks, and its behavior varies with the system. It is therefore a better host-health input than `MemFree`, but it is still an estimate rather than a reservation. See [the kernel's `/proc/meminfo` documentation](https://docs.kernel.org/filesystems/proc.html#meminfo).

The initial controller must not add GTT use to host use as if they were independent physical allocations. It enforces the two canonical observed margins directly:

```text
OBSERVED_gtt  := checked_signed_bytes(min(attested GTT total, applicable configured GTT limit) - mem_info_gtt_used)
OBSERVED_host := checked_signed_bytes(MemAvailable)
```

One new GTT-backed allocation reduces both margins in practice, but its physical bytes are counted once in the predictor and projected onto both constraints.

### PSI reports harm, not bytes

Linux PSI measures time that tasks are stalled by CPU, memory, or I/O contention. Memory `some` means at least some tasks are stalled; memory `full` means all non-idle tasks are stalled simultaneously, with extended time in that state indicating thrashing. PSI supports event triggers over windows from 500 ms to 10 seconds, but it does not report GTT capacity or identify which allocation caused a stall. See [Pressure Stall Information](https://docs.kernel.org/accounting/psi.html).

Consequently:

- GTT and `MemAvailable` provide the numeric constraint observations.
- PSI is authoritative evidence that host-memory contention occurred, and it is useful for severity, diagnostics, and campaign failure or utility analysis.
- PSI is not sufficient evidence that GTT crossed a boundary, is not required before a numeric safety action, and cannot prove recovery of either byte constraint.
- A later catalog revision may add a PSI-based qualitative or rate constraint after its threshold and effect semantics are validated. The initial cell should not.

### Upstream's 90% is policy precedent, not a safety derivation

At upstream commit [`3e532596`](https://github.com/lemonade-sdk/lemonade/commit/3e53259600bf9076d51fd9dee889732e0df0c11a), the global monitor polls every two seconds ([`global_vram_monitor.cpp`](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/global_vram_monitor.cpp#L10-L58)), the eviction engine reacts when the reported fraction crosses one threshold ([`eviction_engine.cpp`](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/eviction_engine.cpp#L38-L50)), and the default is 90% ([`runtime_config.cpp`](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/runtime_config.cpp#L283-L290)). On an AMD APU, upstream computes a global ratio from VRAM and GTT counters but has no independent host floor ([`system_info.cpp`](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/system_info.cpp#L4264-L4328)).

That behavior is useful compatibility context. Its source provides no evidence that 90% is Hatchery's safety limit, recovery target, or correct response-time reserve.

## Live Hatchery observations

The following read-only snapshot was taken at `2026-08-13T14:22:30-04:00`. It is deliberately not a campaign baseline: the machine was busy, the agent's sandbox could not inspect the host process namespace, and a large GPU allocation was present. A later `lemonade status` query showed two resident llama.cpp models, including the downloaded 69.1 GB Qwen3.5 122B artifact, so the allocation must not be classified as external from the sandbox evidence.

| Observation | Value |
| --- | --- |
| Kernel | `7.1.6-1-cachyos` |
| GTT total | `120,259,084,288` bytes, exactly 112 GiB |
| GTT used | `101,733,543,936` bytes, about 94.75 GiB |
| GTT free by subtraction | about 17.25 GiB |
| Nominal VRAM total | exactly 1 GiB; not the model-capacity budget |
| `MemAvailable` | `12,826,556` KiB, about 12.23 GiB |
| Swap | 32 GiB total; about 8.95 GiB free |
| PSI memory | `some avg10=0.29`; `full avg10=0.29` |
| Global GPU attribution | `amd-smi` reported one roughly 76.7 GiB client; its host process was outside the sandbox namespace, while Lemonade reported two resident llama.cpp models |

A 300-generation, 16.36-second observation at nominal 50 ms cadence saw:

- a 16,826,368-byte GTT range;
- a 1,682,677,760-byte `MemAvailable` range; and
- about 0.353 seconds of additional memory `some` PSI and 0.352 seconds of `full` PSI.

This is evidence that `MemAvailable` can move materially on a busy workstation and that the current machine is unsuitable for a calibration attempt without first reconciling all resident and external demand. It is not a counter-noise measurement and must not be used to fit a safety margin.

Reproduce the live inputs with `/sys/class/drm/card1/device/mem_info_gtt_{total,used}`, `/proc/meminfo`, `/proc/pressure/memory`, `amd-smi metric --mem-usage`, and a host-visible process/DRM-fdinfo inventory.

## Threshold model

### Derived safety predicate

Let `d in {gtt, host}` identify one byte-valued constraint. Use this one canonical field namespace throughout:

- `OBSERVED_gtt` be raw current GTT headroom, without subtracting any effect not yet visible in global use;
- `N_gtt` be the frozen observation and counter-noise envelope;
- `L_gtt(W;b)` be every attributed reserved, provisional, or committed internal effect encoded by boundary state `b`, not yet visible, that can materialize over `W`;
- `Ubar_gtt(W;b)` be the frozen conservative bound on unattributed positive GTT-byte demand over `W` from state `b`;
- `S_gtt(W;b)` be the conservative byte effects of every designated controlled helper encoded by state `b` and projected onto GTT over `W`;
- `OBSERVED_host` be raw current `MemAvailable`;
- `N_host`, `L_host(W;b)`, `Ubar_host(W;b)`, and `S_host(W;b)` be the corresponding host terms;
- `RESERVE_d`, `GUARD_d`, `CRITICAL_d`, `ENTRY_d`, and `EXIT_d` be the post-effect reserve and four resolved thresholds for constraint `d`;
- `X_gtt` and `X_host` be remaining read and actuation uncertainty not already included above; and
- `B_d` be the finite closed set of every boundary state reachable under the frozen workload state machine, including all reservation/provisional/committed-effect states, helper in-flight/cap states, admitted external-actor visible/cap states, and uncertainty identities;
- `C_d(W;b) := N_d + L_d(W;b) + Ubar_d(W;b) + S_d(W;b) + X_d` be the prospective future-effect envelope for constraint `d` in boundary state `b`; and
- `Cstar_d(W) := max_{b in B_d} C_d(W;b)` be its frozen worst-case scalar in checked canonical integer bytes.

At every threshold-projection origin `t0`, each enumerated case binds `b_t0 := b` for one `b in B_d`; that state supplies the exact ledger snapshot plus actor/helper visible, cap, in-flight, and remaining-effect values used by `L_d(W;b)`, `Ubar_d(W;b)`, and `S_d(W;b)`. Every envelope and threshold term is a nonnegative canonical byte value and the terms form a disjoint accounting partition. Observed margins use checked signed canonical bytes: when GTT use exceeds effective capacity, `OBSERVED_gtt` remains negative and is never clamped or allowed to wrap, while independent raw-use deltas continue to record further growth. The raw observed margin contains every visible physical allocation. `L_d` contains each server-owned reserved, provisional, or committed effect that can materialize but is not visible; `Ubar_d` contains the frozen prospective unattributed-demand bound; `S_d` contains only not-yet-visible controlled-helper effects; and `N_d`/`X_d` contain distinct noise, read/actuation, and attribution uncertainty. One effect appears in exactly one term and moves from a future term into the raw observation without overlap when observed. Ledger-to-counter fixtures prove this partition for reservation, provisional load, KV growth, helper materialization/release, simultaneous external growth plus owned release, uncertainty, and `mem_info_gtt_used` above effective capacity without underflow or saturation. An incomplete, unbounded, or unrepresentable `B_d` blocks contract freeze.

The first campaign treats 6 GiB as only the initial raw-guard candidate and freezes provisional `RESERVE_gtt=RESERVE_host=2 GiB`. It freezes `W_critical=67.2 seconds + E_critical`, from first physical critical-predicate crossing through latest verified campaign-actor cleanup and complete Lemonade physical/ledger reconciliation, and `W_guard=35.6 seconds + E_guard`, from first physical guard crossing through verified cleanup of every present `CampaignControlledActor`. Both nonnegative uncertainty terms are measured before freeze. With `ceil_GiB` denoting checked upward quantization to whole GiB, derive exactly once for each constraint: `GUARD_d=ceil_GiB(max(6 GiB,RESERVE_d+Cstar_d(W_guard)))`; `CRITICAL_d=ceil_GiB(max(8 GiB,GUARD_d,RESERVE_d+Cstar_d(W_critical)))`; `ENTRY_d=max(16 GiB,CRITICAL_d+1 GiB)`; and `EXIT_d=max(24 GiB,ENTRY_d+8 GiB)`. The contract cannot freeze unless every threshold is finite and physically attainable, `EXIT_d>ENTRY_d>CRITICAL_d>=GUARD_d`, and every boundary-state reserve fixture passes. The controller enters `reclaim` at `ENTRY_d`, enters `critical` immediately at `CRITICAL_d`, and stops controlled growth there. The safety predicates are:

```text
gtt_safe  := OBSERVED_gtt  > CRITICAL_gtt
host_safe := OBSERVED_host > CRITICAL_host
```

`critical` is freshly proven when either predicate becomes false. At the resolved `GUARD_d`, the watchdog commands every controlled actor to release; `Cstar_d(W_guard)` includes crossing overshoot and proves the post-effect reserve for every `b in B_d`. This is not a percentage. Any workload, state-machine, effect, uncertainty, or timing change creates a new campaign-contract revision and reruns the exact derivation; thresholds are never adjusted outside the formula and the residual reserve is never weakened.

No physical validation can prove safety against an arbitrary external process that can allocate an unbounded amount instantly. The validated Hatchery cell must state its external-demand envelope. Exceeding that envelope is an assurance-boundary violation and a campaign failure or invalidation according to the actor, not an excuse to claim universal pressure safety.

### Operator utility thresholds

Ordinary reclaim entry and recovery exit should remain explicit absolute-byte settings for Hatchery:

```text
reclaim_entry when OBSERVED_gtt <= ENTRY_gtt or OBSERVED_host <= ENTRY_host
recover_exit  when OBSERVED_gtt >= EXIT_gtt  and OBSERVED_host >= EXIT_host
```

`ENTRY_gtt` and `ENTRY_host` are the operator's desired coexistence and responsiveness reserves. `EXIT_gtt - ENTRY_gtt` and `EXIT_host - ENTRY_host` are hysteresis. These values may be more conservative than the derived safety predicate; they must never be less conservative once the safety terms are known.

### Initial experiment values

Use these only for the first controlled sweep:

| Parameter | Initial value | Meaning |
| --- | ---: | --- |
| GTT provisional minimum entry coordinate | 16 GiB free | The frozen `ENTRY_gtt` is raised by deterministic normalization when required; phase entry is immediate and hard action remains dwell-gated. |
| GTT provisional minimum critical coordinate | 8 GiB free | The frozen boundary is `CRITICAL_gtt`, which is raised when the derived threshold is larger. |
| GTT provisional minimum exit coordinate | 24 GiB free | The frozen `EXIT_gtt` is raised to remain at least 8 GiB above entry. |
| Host provisional minimum entry coordinate | 16 GiB `MemAvailable` | The frozen `ENTRY_host` is raised by deterministic normalization when required. |
| Host provisional minimum critical coordinate | 8 GiB `MemAvailable` | The frozen boundary is `CRITICAL_host`, which is raised when the derived threshold is larger. |
| Host provisional minimum exit coordinate | 24 GiB `MemAvailable` | The frozen `EXIT_host` is raised to remain at least 8 GiB above entry. |
| Hard-action dwell | 1 second | Continuous fresh reclaim-or-worse qualification measured by monotonic elapsed time. |
| Critical transition | First fresh coherent observation proving the conservative predicate false | Immediate transition; no confirmation dwell. |
| Recovery stabilization | 5 seconds | Continuous fresh qualification above every applicable exit boundary measured by monotonic elapsed time. |

These absolute values are intentionally easy to reason about and leave room for 1 GiB helper steps. They are not promoted defaults. The first sweep must measure whether they preserve interactivity, avoid OOM or allocation failure, and reclaim too early or too late. The accepted values should then be frozen in the Hatchery catalog rule and evidence manifest.

Using the same 16/8/24 GiB sweep coordinates for GTT and host memory is an experimental convenience, not a claim that the two constraints have equal risk or uncertainty. The physical results may produce different accepted thresholds.

At the provisional 16 GiB minimum entry coordinates, the live snapshot above would leave GTT just outside ordinary entry while the roughly 12.23 GiB `MemAvailable` value entered host `reclaim`; a hard action would remain dwell-gated. The resolved entries may be higher, so the snapshot cannot predict the frozen phase before derivation. It still illustrates that the host floor may dominate while GTT remains within its utility reserve, but cannot establish that 16 GiB is safe or desirable.

The utility decision that must remain operator-chosen is the preferred 16 GiB entry reserve. The campaign may disprove it as unsafe; only the operator can decide whether a larger safe value is worth earlier reclamation or whether a smaller still-safe value better serves model residency.

## Host watchdog and cgroup evidence

Run both pressure helpers in dedicated cgroup v2 leaves. The kernel documents `memory.current`, `memory.peak`, `memory.high`, `memory.max`, and `memory.events`; `memory.events` includes `high`, `max`, `oom`, and `oom_kill`. `memory.high` throttles rather than invoking the OOM killer, while `memory.max` may invoke the cgroup-local OOM killer if reclaim fails. See [cgroup v2 memory controls](https://docs.kernel.org/admin-guide/cgroup-v2.html#memory-interface-files).

For promotion tests:

- Keep the watchdog outside Lemonade and every `CampaignControlledActor` cgroup.
- Require a controller heartbeat every 250 ms. A one-second heartbeat gap aborts and commands every present `CampaignControlledActor` to release. Independently sample both guard signals often enough to bound first physical guard crossing through guard detection, coherent decision, and release-command dispatch to 100 ms. Control-pipe closure must also make each actor self-release. Missing acknowledgment at 500 ms or incomplete physical release at the 30-second baseline deadline makes the independently owned watchdog forcibly stop every actor cgroup and verify cleanup within the separately prequalified five-second bound.
- Stop increasing controlled pressure at the derived `CRITICAL_gtt` or `CRITICAL_host` boundary. Independent out-of-band resolved `GUARD_gtt`/`GUARD_host` triggers command every present `CampaignControlledActor` to release immediately. The initial candidate is 6 GiB, but only the resolved thresholds are operational. The separate 2 GiB `RESERVE_gtt`/`RESERVE_host` values are retained after bounded future effects by the `Cstar_d(W_guard)` proof.
- Treat any new system or helper-cgroup `oom`, `oom_kill`, unexpected `max`, process death, missed watchdog deadline, or failure to release the helper as a failed attempt.
- Use `memory.high` only to contain the anonymous host-memory helper; do not interpret its expected `high` events as system pressure. Do not assume that cgroup memory controls bound GPU mappings until physical evidence proves the charge path. Do not use `memory.max` as the normal stop mechanism because reaching it can invoke cgroup OOM.
- Record swap counters and PSI. Swap availability does not enlarge `MemAvailable`, the GTT mapping limit, or the predictor's capacity credit.

Before physical execution, prove each resolved guard preserves its separate post-effect reserve for every `b in B_d` over `W_guard`. Its 35.6-second maximum includes 100 ms from first physical crossing through guard sampling/read/decision and release-command dispatch, 500 ms to acknowledgment, 30 seconds to physical-zero reconciliation, and 5 seconds to forced cleanup after a miss. `Cstar_d(W_guard)` includes demand and crossing overshoot during detection. Acknowledgment does not count as physical release. A cleanup miss blocks further physical runs. Any failed state fixture, infeasible threshold, or changed state set blocks the campaign-contract revision; do not accelerate the test or refit after results.

## Sampling, skew, and demand envelope

### Two sampling roles

Use separate streams:

| Stream | Initial cadence | Maximum gap | Purpose |
| --- | ---: | ---: | --- |
| Independent campaign oracle | 50 ms | 100 ms | Capture physical peaks, transitions, and helper effects. |
| Candidate runtime controller | 250 ms | 400 ms read-initiation gap | Exercise the behavior proposed for the first Hatchery cell. |

The 50 ms oracle cadence matches the kernel PSI monitor's minimum documented update interval for a 500 ms trigger window. It does not imply that PSI and byte counters are synchronized. The independent oracle and candidate controller retain separate identities and cannot satisfy each other's evidence obligations.

For every generation, record monotonic timestamps immediately before and after each source read. The initial maximum cross-source generation skew is 25 ms. The controller initiates the next read no later than 400 ms after the prior read initiation and completes the coherent read, validation, and atomic publication within 100 ms, including the skew allowance; thus physical crossing to publication remains at most 500 ms. A generation that exceeds any phase limit is incoherent and cannot authorize action or recovery. Such a gap resets continuous dwell or recovery qualification and cannot be bridged by sample count. Runtime byte evidence becomes stale after 750 ms. Dwell and stabilization use monotonic elapsed time from the first continuously qualifying fresh observation. These are campaign parameters; retain the actual timestamp intervals so the catalog can revise them from evidence.

### Noise and baseline procedure

Do not estimate counter noise from the busy snapshot above. Before each boot epoch:

1. Reconcile Lemonade's ledger and prove that no test-owned backend/helper process remains.
2. Record host-visible DRM clients and all relevant cgroups.
3. Hold a quiescent 15-minute baseline at 50 ms oracle cadence.
4. Reject the baseline if GTT has an unexplained trend, host PSI shows sustained contention, or an unattributed process changes the domain beyond the proposed envelope.
5. Freeze `N_gtt` and `N_host` from the maximum plateau range plus measured read/skew uncertainty. Do not use a percentile for the safety term.

The expected 1 GiB helper step must dominate the frozen noise envelope. If it does not, increase plateau time or reduce environmental noise rather than hiding it statistically. The sole pacing and projection contract is `HelperStepState` below.

### Unattributed demand

The campaign-contract actor table is closed. For each `b in B_d`, admitted unattributed actor `a`, and constraint `d`, it freezes visible effect `V_a,d(b)`, a physically enforced or protocol-verified total effect cap `K_a,d`, maximum instantaneous burst `B_a,d`, step/rate quantum `Q_a,d`, and minimum interval `T_a`. Its prospective contribution is `E_a,d(W;b)=min(max(0,K_a,d-V_a,d(b)), B_a,d+ceil(W/T_a)*Q_a,d)` for `W>0`, and zero at `W=0`; `Ubar_d(W;b)=sum_a E_a,d(W;b)`. `A_unattributed` is empty for ordinary rows and contains exactly the one pinned `H-EXT-01` process/cgroup/script identity for that row; quiescent background is covered only by `N_d`. Thus ordinary rows freeze `Ubar_d(W;b)=0` for every `b`, while `H-EXT-01` uses that actor's exact frozen `E_a,d(W;b)`. Every other positive unattributed actor has a zero allowance, so its appearance aborts and invalidates the attempt. A cap/rate that is not physically enforced or conservatively verified is unknown and blocks freezing rather than receiving capacity credit.

For validation, each retained attempt binds its actual starting/crossing snapshot to exactly one enumerated `b_t0 in B_d`; an unmatched snapshot blocks action and invalidates the attempt. Let `R_d(w)` be the raw observed demand-direction change from that `t0`, and let `Alo_d(w)` be the conservative lower bound on the signed net change caused by every identity-bound Lemonade/helper materialization and release. Define realized unattributed demand as `D_d(W)=sup_{0<=w<W} max(0, R_d(w)-Alo_d(w))`; favorable owned release cannot mask external growth. The disjoint observation/noise allowance is `N_d`, and attribution uncertainty belongs in `X_d`; neither is included in `Alo_d` or `Ubar_d`. Every valid run requires `D_d(W)<=Ubar_d(W;b_t0)+N_d+X_d` for every applicable prefix/horizon; a breach follows the actor/phase table and cannot refit any allowance.

One `HelperStepState` contract owns pacing and the `S_d(W;b)` projection. Each deterministic helper allocates 1 GiB steps, touches and reads each back, and permits at most one commanded-but-not-yet-physically-observed step; it may issue a new command only after the prior step is acknowledged in one coherent observation and at least two seconds have elapsed from that acknowledgment. For helper `h` in state `b`, let `q_h(b)` state whether one command is in flight; `I_h,d(b)` be its not-yet-visible effect, which is `0` when `q_h(b)=false`; `B_h,d` one step; `T_h=2 seconds`; and `R_h,d(b)` the remaining conservative not-yet-visible effect on constraint `d`, including any in-flight step. For `W>0`, its `S_d(W;b)` contribution is `min(R_h,d(b), I_h,d(b) + future_h(W;b)*B_h,d)`, where `future_h(W;b)=ceil(max(0,W-T_h)/T_h)` when `q_h(b)=true` and `ceil(W/T_h)` otherwise; it is zero at `W=0`. Reconciliation fixtures cover unequal GTT and host projections. A zero projection requires physical evidence bound to the exact helper source/binary/config/toolchain/run identity. Missing or unmatched evidence uses a conservative nonzero/unknown projection and blocks freezing if unbounded.

The campaign-contract manifest owns distinct typed monotonic milestones and maximum deadlines for first physical predicate or guard crossing, guard sampling/read/decision, coherent-read initiation, qualifying controller publication, plan outcome/action reservation, action-command dispatch, controlled-demand release command and acknowledgment, first verified useful physical effect, verified Lemonade physical/ledger release, forced helper stop, and verified helper cleanup. Guard crossing through release-command dispatch is bounded to 100 ms. The initial action-success maxima are:

| Interval | Maximum |
| --- | ---: |
| First physical predicate crossing to coherent-read initiation | 400 ms |
| Read initiation through qualifying controller publication | 100 ms |
| Controller publication to terminal no-action outcome or complete action reservation | 1 second |
| Action reservation to action-command dispatch | 100 ms |
| Action-command dispatch to first independently observed useful physical release effect | 15 seconds |
| First useful physical effect to full physical and ledger reconciliation | 15 seconds |

The action portion of a successful path is therefore 31.6 seconds plus separately frozen uncertainty. No phase may borrow unused time from another.

Every path, including successful reclamation, follows the same typed terminal edges:

| Interval | Maximum |
| --- | ---: |
| Terminal action/no-action outcome to release-command dispatch | 100 ms |
| Release command to acknowledgment, without crediting physical release | 500 ms |
| Acknowledgment to helper-identity physical zero and both global constraints reconciled | 30 seconds |
| Missing acknowledgment or missed baseline deadline to forced-stop command and verified cleanup of every present `CampaignControlledActor` cgroup | 5 seconds |

`CampaignControlledActorSet` contains both deterministic helpers and, during `H-EXT-01`, the pinned PyTorch external process/cgroup. The 30-second clock begins at acknowledgment. Every path releases every present actor. Identity-bound physical zero requires each actor's allocation manifest/control identity to show no live allocation, no process/cgroup charge, and no GPU context/DRM fdinfo where available, with identity-scoped expected deltas reconciled; global margins corroborate but cannot prove cleanup. A missed deadline commands forced stop of every actor cgroup, with verified cleanup inside the existing five-second bound. No experiment freezes unless that path demonstrates its finite bound. The maximum `W` remains 67.2 seconds plus uncertainty.

Use one frozen actor-by-phase disposition from baseline start through verified cleanup. Terminalization is orthogonal to evidence disposition: every table cell enters the same idempotent helper-release/forced-cleanup state machine; when no helper, cgroup, or physical effect exists it records a verified no-op.

| Actor / condition | Before stimulus | Stimulus, response, or cleanup |
| --- | --- | --- |
| Realized external demand has `D_d(W)>Ubar_d(W;b_t0)+N_d+X_d` for any applicable prefix/horizon | Retain as `infrastructure_invalid`; start no stimulus; terminalize existing helper state. | Retain as `infrastructure_invalid` and terminalize. Any independently observed DUT invariant violation remains a product failure. |
| Lemonade/backend exceeds its owned envelope | Product failure and terminalize. | Product failure and terminalize. |
| Designated helper exceeds its protocol/effect envelope | `helper_invalid`; start no further stimulus; terminalize. | `helper_invalid` and terminalize; a forced-cleanup miss is a campaign safety failure. |
| Predeclared oracle/harness data loss | `infrastructure_invalid`; start no stimulus; terminalize. | `infrastructure_invalid` and terminalize. |
| Watchdog deadline or verified-cleanup miss | Campaign safety failure; terminalize if possible and block physical runs. | Campaign safety failure; terminalize if possible and block physical runs. |
| Operator abort for an unrelated event | Retain as `infrastructure_invalid` and terminalize. | Retain as `infrastructure_invalid` and terminalize. |

Every outcome and simultaneous independent failure is recorded; invalidation never erases an already observed product failure. Production behavior outside the bounded external-demand assumption remains best-effort and says so explicitly.

## External GTT helper

Use a small pinned-source native HIP program as the deterministic pressure generator, not PyTorch.

HIP documents `hipMalloc()` as pinned device allocation and `hipMemset()` as writing device memory. See [HIP memory allocation](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_runtime_api/memory_management.html#memory-allocation) and the [HIP memory API](https://rocm.docs.amd.com/projects/HIP/en/latest/doxygen/html/group___memory.html). Combined with AMD's Strix Halo documentation that AI device allocations use GTT-backed shared memory, this gives the narrowest reviewable generator.

The helper should:

- use the exact campaign HIP/ROCm toolchain and record its binary and source digests;
- allocate one bounded chunk at a time with `hipMalloc`;
- initialize it with HIP and run a readback/checksum kernel before acknowledging the step;
- report allocation/free timestamps and return codes over a control pipe;
- hold each chunk until the harness explicitly releases it;
- never join Lemonade's cgroup, process tree, ledger, or recovery authority; and
- reconcile its allocation/control identity, process/cgroup state, and GPU context/DRM fdinfo where available against the expected `B_h,d` in every affected GTT and host constraint before it is accepted as an actuator. A global GTT delta is corroborating evidence, not causal proof; an unreconciled projection remains conservative unknown and rejects the actuator.

PyTorch carries allocator caching, framework initialization, Python packaging, and model/runtime effects that make it a poor deterministic actuator. Use a pinned PyTorch/ROCm workload later as the required real independent application row, not as the threshold-setting helper.

## Workload partition for the first predictor campaign

Do not choose the partition only from what happens to be downloaded. The accepted v1 predicate excludes embedding, reranking, multimodal/projector, MTP/draft/speculative, vLLM, and auxiliary-model paths. It also fixes one slot, empty custom llama.cpp arguments, and disabled prompt RAM/context-checkpoint caches. See [Profile-free residency estimation](profile-free-residency-estimation.md#v1-configuration-predicate).

The current Hatchery cache contains several useful text-only candidates, but three downloaded names explicitly select MTP and are negative applicability fixtures rather than predictor evidence: `Qwen3.5-4B-MTP-GGUF-UD-Q4_K_XL`, `Qwen3.6-27B-MTP-GGUF-UD-Q4_K_XL`, and `Qwen3.6-35B-A3B-MTP-GGUF-UD-Q4_K_XL`. Embedding/reranking models and `Qwen3.5-0.8B-FP16-vLLM` likewise remain outside this cell.

Ordinary request-side overrides cannot reliably erase inherited `llamacpp_args` or `ModelInfo.mmproj`: empty recipe-option strings are omitted, lower recipe layers are then merged, and the projector comes from model metadata independently. The campaign therefore needs its own immutable model manifest. Each entry references exactly one local main GGUF, carries no projector, MTP/speculation label, `hf_load`, auxiliary component, or model-level arguments, and runs under an isolated runtime configuration with empty global `llamacpp.args`. Required fixed v1 flags are normalized server/harness fields, not arbitrary recipe arguments.

After downloading, inspecting, hashing, and proving pure-offline sizing applicability for every artifact, use this partition:

| Partition | Candidate | Current state | Role |
| --- | --- | --- | --- |
| Calibration | `Llama-3.2-1B-Instruct-GGUF` | Download required | Small dense/GQA and ordinary quantized baseline. |
| Calibration | main GGUF of `Qwen3.5-4B-heretic-v2-i1-GGUF-Q4_K_M` | Downloaded | Small current-family hybrid/SSM coverage and a distinct GGUF producer/encoding; discard the user recipe's custom sampling arguments. |
| Calibration | `LFM2-8B-A1B-GGUF` | Download required | Hybrid/recurrent applicability, if source dry construction classifies every component. |
| Calibration | `Qwen3-30B-A3B-GGUF` | Download required | Medium mixture-of-experts geometry with a text-only catalog artifact. |
| Held-out | main GGUF of `gemma-4-E2B-it-GGUF-Q8_K_XL` | Downloaded | Different family and Q8 encoding; omit its distinct projector. |
| Held-out | main GGUF of `gemma-4-26B-A4B-it-GGUF-Q4_K_XL` | Downloaded | Held-out Gemma scale and mixture-of-experts shape; omit its distinct projector and user recipe arguments. |
| Held-out | `Qwen2.5-Coder-32B-Instruct-GGUF` | Download required | Large conventional-dense text-only replacement for the multimodal Qwen3.5 27B candidate. |
| Held-out | main GGUF of `trohrbaugh-Qwen3.5-122B-A10B-heretic-i1-GGUF-Q4_K_M` | Downloaded | Near-capacity mixture-of-experts/hybrid model and distinct producer; discard the user recipe's BF16-KV and sampling arguments. |

Run calibration at explicit 8 Ki-token and 32 Ki-token contexts where the source metadata permits them, with fixed `parallel=1`, `batch-size=2048`, and `ubatch-size=512`. A held-out model runs at 16 Ki only when its analytic envelope proves both accepted Q48 guard inequalities; otherwise the contract records a predeclared `out_of_envelope` 16 Ki case without spawning it. Each held-out model also runs the largest physically feasible 4 Ki-aligned context whose analytic envelope preserves both guard inequalities. Bind their values and campaign-contract digest into every run manifest. This prevents an unsafe case from running and prevents an inconvenient architecture maximum from being silently omitted. Every executed model/context traverses cold load, near-empty context, maximum prompt batch, near-target retained context, one generated-token step beyond the prompt, and verified unload/release.

If LFM2 does not pass pre-campaign estimator support inspection, remove that role from v1 before freezing rather than substitute after results. The split cannot freeze until the four absent artifacts are downloaded and hashed, the exact refreshed `rocm-stable` 7.13 backend/dependency closure is installed and attested instead of Hatchery's current b10236 backend, the campaign-only fixed-field launch path exists, and finite low/high context and KV types are fixed per artifact. Do not widen the predicate, move a model between partitions, or replace an inconvenient model after observing promotion results. `Tiny-Test-Model-GGUF` remains a lifecycle/race control, not residual calibration evidence.

## Cold-attempt and reset taxonomy

Use explicit terms rather than one ambiguous word `cold`:

| Term | Required reset |
| --- | --- |
| `warm` | Same daemon and backend epoch; useful only for steady-state and repeated-request coverage. |
| `daemon-cold` | Prior daemon is proven dead/fenced, a new daemon epoch starts, and durable recovery/claim replay completes. |
| `runtime-cold` | `daemon-cold`, plus every test-owned backend/helper process and GPU context is gone, the ledger is reconciled, and GTT/host observations return to the frozen baseline envelope for five continuous seconds. |
| `boot-cold` | `runtime-cold` after the host boot ID changes; driver, ROCm, and service initialization start in a new boot epoch. |

Recommendation:

- Run every mandatory physical row in three independent `runtime-cold` attempts.
- Use three distinct `boot-cold` campaign epochs. Repeat topology, signal, predictor-boundary, release, and recovery sentinel rows in every boot epoch; distribute the remaining required attempts across the three boots rather than running every row nine times.
- Run deterministic race and crash interleavings three times each from the reset level required by the fault. A daemon-restart fault requires at least `daemon-cold`; driver/context-release claims require `runtime-cold` or `boot-cold` evidence.
- Allow up to 30 seconds for a test-owned GPU context and counters to return to baseline, while requiring the final five-second stable interval. A timeout, leak, or unexplained residual after a Lemonade/backend action is a product failure, not an invalid attempt.

Attempt disposition uses the actor-by-phase table above at every instant through verified cleanup. Preserve every invalid bundle and reason. A predictor exceedance, protected eviction, wrong-process action, backend crash, release timeout, stale-evidence action, OOM event, watchdog miss, residual owned allocation, or independently observed DUT violation is a failed attempt even when a simultaneous infrastructure event also invalidates another evidence claim.

## Predictor utility gate

Safety and utility are separate predictor gates. Assignment of `validated_predictor` capacity credit requires zero valid observed lifetime effects above the frozen bound. Capacity-credit enablement additionally requires both:

1. For every held-out in-envelope single-model case and every automated byte-valued constraint projection `d`, prediction overhead `P_d - O_d` satisfies:

   ```text
   0 <= P_d - O_d <= max(4 GiB, ceil(15 * O_d / 100))
   ```

   `P_d` is the frozen conservative predicted lifetime effect and `O_d` is the independently observed peak lifetime effect in canonical integer bytes. Compare each constraint projection separately. Count one shared physical allocation once in its allocation group and project it onto each genuinely affected constraint; never sum GTT and host bytes.
2. At least one predeclared physically feasible two-resident composition remains predicted admissible while preserving both accepted pressure guards.

A predictor revision that stays above every safety observation but misses either utility condition supplies report-only footprint evidence until a new predictor/catalog revision tightens the bound and passes a newly frozen held-out campaign. Passing this gate never authorizes victim selection or a destructive action; pressure reclamation, ownership, protection, action reservation, process termination, recovery, and verified release retain separate capability cells. An optional exact-model profile cannot rescue or retroactively change the predictor-rule revision's held-out utility result.

## NPU and FLM status

Physical NPU/FLM conflict validation is not available on Hatchery now.

The 2026-08-13 read-only inspection found:

- an AMD XDNA2 PCI function `1022:17f0` at `0000:c7:00.1`;
- the in-tree `amdxdna` module loaded, but the PCI function had `enable=0`, no bound `driver` symlink, and no `/sys/class/accel/accel*` device;
- FLM `0.9.46` installed, matching [current upstream Lemonade's `v0.9.46` pin](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/resources/backend_versions.json#L132-L134);
- `flm validate --json` reported `ready=false`, `amd_device_found=false`, and no devices; and
- `flm list --filter installed --json` reported no installed models.

FastFlowLM's official repository says FLM supports Strix Halo XDNA2 and Linux, so the platform is a plausible future target rather than intrinsically unsupported. See [FastFlowLM](https://github.com/FastFlowLM/FastFlowLM) and the [`v0.9.46` release](https://github.com/FastFlowLM/FastFlowLM/releases/tag/v0.9.46).

Current upstream Lemonade runs `flm validate` and treats `ready=false` or `amd_device_found=false` as a validation failure ([`fastflowlm_models.cpp`](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/backends/fastflowlm/fastflowlm_models.cpp#L671-L727)); a load refuses before download/spawn when that check fails ([`fastflowlm_server.cpp`](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/backends/fastflowlm/fastflowlm_server.cpp#L160-L178)).

Treat `modeled` as the proposed evidence ceiling for cell `H-NPU-FLM-CONFLICT-XDNA2-v1`, not its current status. Its conjunctive gate is the synthetic portion of base row `H-NPU-01`, `H-PROT-01..02`, `H-CON-01..02`, `H-EVD-01`, `H-REC-01`, `H-EXP-01`, and overlay row `H-LIV-01`; each row is evaluated for the NPU/FLM compatibility, live device/topology evidence, and ownership domains rather than borrowing a ROCm result. Device unavailability alone does not earn it. The cell remains at its separately reported delivery/evidence state until primary architecture evidence, complete normalized contracts and analytic bounds, those deterministic synthetic conflict/protection/concurrency/recovery and explanation/liveness tests, and fallback `hatchery_npu_conflict_preserve_refuse_v1` pass. That fallback preserves every incumbent, including unpinned-idle residents, and refuses the incoming conflicting NPU/FLM load; automatic displacement remains unavailable until the exact physical conflict/release cell validates. Open physical validation only after a host-visible run proves a bound amdxdna device, accessible `/dev/accel`, `flm validate` ready, at least the required LLM/embedding/audio model types installed, and a clean FLM load/unload baseline. NPU/FLM protection semantics still remain implementation requirements; only the physical capability label is deferred.

## Evidence storage for a sole-maintainer fork

Use a three-tier policy:

1. **Git:** Commit run manifests, catalog/predictor identities, normalized summaries, small sanitized traces/fixtures, failure records, SHA-256 digests, and review/acceptance attestations. This is the durable audit index.
2. **GitHub Release assets:** Put large sanitized `.tar.zst` bundles under content-addressed names in a dedicated evidence release series. GitHub permits up to 1,000 assets per release, each under 2 GiB, with no total release-size or bandwidth limit. Every manifest row binds the digest, byte size, primary locator, independently administered backup identity and locator, retention class, and reference-liveness roots. See [GitHub release quotas](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases#storage-and-bandwidth-quotas).
3. **Private durable storage, only if needed:** Put unredacted process/driver diagnostics in an encrypted content-addressed store with a second backup. Commit only its digest, retention class, redaction relationship, and access-controlled locator. Stop before collecting sensitive data; the operator must explicitly authorize the primary store, independently administered backup, retention period, access policy, deterministic sanitized-derivation contract, and reference-liveness roots.

Release assets are administratively replaceable or deletable through GitHub's API, so the committed digest and independently administered backup supply tamper/loss detection and restoration. Before collection, the operator separately selects and authorizes the backup provider and the anti-rollback witness provider/identity/key. The witness must have an administrative and append-authority boundary independent of the local journal and primary asset host; a backup provider may also operate it only when that separation is explicit. Do not infer either identity in the harness or claim the asset service itself is immutable. See [GitHub's release-asset API](https://docs.github.com/en/rest/releases/assets).

Promotion and continuing evidence authority use a signed monotonic `EvidenceLivenessAttestation` chain plus an independently administered anti-rollback witness selected and authorized with the backup provider. Each record contains `sequence`, `previous_digest`, `promotion_index_digest`, dependent cell IDs, `issuer_key_id`, daemon/boot epoch, `checked_at`, `expires_at`, both locator identities, expected and observed digests and byte sizes, every active reference-liveness root's reachability, and success/failure. The authorized Ed25519 issuer-key digest and witness identity are bound by the promotion index and catalog candidate. A fixed-size, pre-reserved local `evidence_liveness_head` CAS-publishes the latest sequence/digest. The witness append is a conditional append against its exact current `(sequence,digest,checkpoint)`: it accepts only the next sequence whose predecessor is that exact tip, rejects an equal-sequence competing digest and every non-tip predecessor, and returns a signed checkpoint for the new tip. Readers reject any local/witness disagreement, unavailable witness, invalid signature/predecessor/sequence, fork, or rollback.

A successful check issues a witness-receipted `EvidenceLivenessLease` whose expiry is the earliest of five minutes after issuance, its attestation expiry, and the underlying full-evidence-verification expiry. `EvidenceLivenessCapGeneration` is the one fail-closed generation: the lease records it, every token binds it, and every failure, resume, or restart atomically increments it before publication. The lease also contains signed boot ID, daemon epoch, local-head generation/digest, witness checkpoint, `CLOCK_BOOTTIME` issuance timestamp, and clamped `CLOCK_BOOTTIME` expiry. A successful monotonic head/checkpoint advance or healthy renewal may issue another lease but does not revoke an older unexpired lease with the current cap generation; immediate invalidation is reserved for cap-generation change, rollback/fork evidence, witness rejection/unavailability, exact-lease expiry/signature mismatch, resume, or restart. The server revalidates those conditions at every member of `LivenessRevalidationBoundary` and applies the canonical `H-LIV-01f` disposition on failure. Renewal begins no later than four minutes after issuance or earlier when required by the clamped expiry; the witness request has a 30-second deadline and must leave 30 seconds before expiry for fail-closed publication and token invalidation. A plan cannot start unless its complete bounded horizon ends before its bound lease and underlying authority expire. Suspend-inclusive `CLOCK_BOOTTIME` prevents sleep from extending a lease; resume requires a live witness query plus fresh evidence verification before readiness. Leases never survive daemon restart, which has the same readiness rule. Detection of any failure revokes renewal and attempts local/witness failure publication after the cap increment. Publication failure stays fail-closed and blocks readiness.

Full evidence verification runs before promotion/catalog publication, is scheduled for renewal by day 29, and expires exactly 30 days after its successful check; a missed renewal target leaves the current authority valid only until that hard expiry. It also runs immediately after any locator, retention-root, storage-health, suspend/resume, or daemon/boot change. Within a boot, age uses `CLOCK_BOOTTIME` elapsed time bound at issuance. Clock rollback, unavailable synchronization, or uncertain cross-boot age prevents issuance until reverification. The central per-cell effective-mode function computes the conservative meet of configured intent, delivery state, catalog capability/evidence ceiling, this liveness cap, live signal/evidence health, and recovery readiness. A liveness failure caps only its dimension at the cell's registered fallback while preserving immutable history; authority returns only after a fresh successful chain tip, witness checkpoint, and lease.

Do not use Actions artifacts as the durable store. Public-repository artifact retention is at most 90 days. See [GitHub Actions artifact retention](https://docs.github.com/en/organizations/managing-organization-settings/configuring-the-retention-period-for-github-actions-artifacts-and-logs-in-your-organization).

Do not make Git LFS the default for raw campaigns. GitHub Free includes 10 GiB storage and 10 GiB monthly bandwidth, and every changed large object consumes a new full object. See [Git LFS billing](https://docs.github.com/en/billing/concepts/product-billing/git-lfs). It remains an option for small, stable fixtures whose clone-time availability is worth that cost.

Prefer collection-time minimization: do not record prompts, API keys, environment secrets, full unrelated process arguments, user files, or arbitrary memory. Sanitize deterministically before publication and bind the sanitized bundle to the original digest. If all promotion evidence can be collected without sensitive content, omit the private tier.

## Concrete recommended answers for Issue 35

1. **Thresholds:** Use the canonical derived critical predicates and deterministic normalization above. Treat 16/8/24 GiB as provisional entry/minimum-critical/exit coordinates only: freeze each executed profile as `ENTRY_d=max(16 GiB,CRITICAL_d+1 GiB)` and `EXIT_d=max(24 GiB,ENTRY_d+8 GiB)`, with one-second hard-action dwell and five-second recovery stabilization. Pressure phases still enter immediately.
2. **Host floor:** Make `MemAvailable` an independent constraint on the same RAM. Add cgroup and watchdog evidence, but never substitute swap, PSI, or a GTT percentage for it. The 16 GiB preferred entry reserve remains operator-chosen.
3. **PSI:** Corroborating/severity evidence for the initial cell; authoritative only that stalls occurred, not for byte capacity or GTT attribution.
4. **Sampling:** Use a 50 ms independent oracle and 250 ms candidate controller, 25 ms maximum generation skew, 750 ms freshness, and canonical `HelperStepState`: one 1 GiB command may be in flight, and the next may issue no earlier than two seconds after coherent acknowledgment. Use the closed external-actor envelope above. Revise these values only through a new frozen campaign manifest.
5. **GTT helper:** Native HIP first; PyTorch only as a later representative external application.
6. **Workload split:** Freeze the three-or-four-model calibration partition (depending on predeclared LFM2 support) and four-model held-out partition above only after artifact/GGUF inspection. Keep MTP and other excluded shapes as negative applicability fixtures and lifecycle controls out of residual evidence.
7. **Cold attempts:** Three runtime-cold attempts per physical row across three boot-cold epochs, with sentinel rows repeated each boot. Treat owned leaks and controller/backend failures as failures, never infrastructure invalidations.
8. **NPU/FLM:** Treat `modeled` as the proposed evidence ceiling; physical promotion is blocked by the presently disabled/unbound NPU and empty FLM model store, while modeled status still requires its complete nonphysical evidence.
9. **Evidence:** Git index plus sanitized GitHub Release assets, one independently administered digest-verified backup, and an explicitly authorized anti-rollback witness provider/identity/key; Actions only for transit; an operator-selected encrypted private store only if collection cannot avoid sensitive diagnostics.
10. **Predictor utility:** Require the per-projection overhead and feasible two-resident gates above before capacity credit; keep a safe but impractical predictor report-only.

## Operator choices that remain

The research does not settle these value judgments:

1. Is preserving 16 GiB of ordinary GTT and host headroom the desired Hatchery utility tradeoff, subject to the campaign proving it safe, or should the experiment start with a larger reserve?
2. Is a five-second recovery stabilization acceptable, or is avoiding repeated unload/reload more important than faster recovery exit?
3. May sanitized physical evidence be public in `nisavid/lemonade` Release assets, which independently administered backup provider is authorized, and which anti-rollback witness provider/identity/key is authorized under the required independent append-authority boundary?
4. If unredacted evidence is unavoidable, which primary encrypted store, independently administered backup, retention period, access policy, deterministic sanitized-derivation contract, and reference-liveness roots are authorized? Collection remains blocked until all six are explicit.

All other recommendations above can be accepted as the initial campaign contract and revised only if physical evidence contradicts them.
