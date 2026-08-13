# Hatchery residency campaign parameters

Status: research proposal for Issue [#35](https://github.com/nisavid/lemonade/issues/35), not accepted policy, an implementation plan, or validation evidence.

Date: 2026-08-13

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

The initial controller must not add GTT use to host use as if they were independent physical allocations. It should enforce both:

```text
gtt_headroom  = effective_gtt_total - observed_gtt_use
host_headroom = observed_MemAvailable
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

Let:

- `H_gtt` be current GTT headroom after subtracting only reservations not already visible in global use;
- `N_gtt` be the frozen observation and counter-noise envelope;
- `L_gtt` be the maximum committed internal effect that may not yet be visible;
- `E_gtt(W)` be the maximum permitted unattributed positive demand over response horizon `W`;
- `A` be current `MemAvailable`;
- `N_host`, `L_host`, and `E_host(W)` be the corresponding host terms; and
- `F_emergency` be the operator-approved minimum host availability at which the test watchdog must already be stopping controlled pressure.

The safety predicates are:

```text
gtt_safe(W)  := H_gtt > N_gtt + L_gtt + E_gtt(W)
host_safe(W) := A > F_emergency + N_host + L_host + E_host(W)
```

`critical` is freshly proven when either predicate becomes false for the critical response horizon. This is not a percentage. It moves when the measured response bound, signal uncertainty, helper rate, or internal lifetime envelope changes.

No physical validation can prove safety against an arbitrary external process that can allocate an unbounded amount instantly. The validated Hatchery cell must state its external-demand envelope. Exceeding that envelope is an assurance-boundary violation and a campaign failure or invalidation according to the actor, not an excuse to claim universal pressure safety.

### Operator utility thresholds

Ordinary reclaim entry and recovery exit should remain explicit absolute-byte settings for Hatchery:

```text
reclaim_entry when H_gtt <= GTT_ENTRY or A <= HOST_ENTRY
recover_exit  when H_gtt >= GTT_EXIT  and A >= HOST_EXIT
```

`GTT_ENTRY` and `HOST_ENTRY` are the operator's desired coexistence and responsiveness reserves. `GTT_EXIT - GTT_ENTRY` and `HOST_EXIT - HOST_ENTRY` are hysteresis. These values may be more conservative than the derived safety predicate; they must never be less conservative once the safety terms are known.

### Initial experiment values

Use these only for the first controlled sweep:

| Parameter | Initial value | Meaning |
| --- | ---: | --- |
| `GTT_ENTRY` | 16 GiB free | Ordinary pressure episode enters after dwell. |
| GTT provisional critical boundary | 8 GiB free | Temporary campaign bound until the derived predicate has measured terms. |
| `GTT_EXIT` | 24 GiB free | Recovery target, giving 8 GiB of hysteresis. |
| `HOST_ENTRY` | 16 GiB `MemAvailable` | Ordinary host-pressure entry. |
| Host provisional critical boundary | 8 GiB `MemAvailable` | Temporary campaign bound, not a claim that 8 GiB is universally safe. |
| `HOST_EXIT` | 24 GiB `MemAvailable` | Recovery target, giving 8 GiB of hysteresis. |
| Noncritical dwell | 1 second | Four consecutive fresh 250 ms generations. |
| Critical confirmation | Two coherent generations 100 ms apart | Bypasses ordinary dwell but not freshness/coherence. |
| Recovery stabilization | 5 seconds | Twenty consecutive fresh 250 ms generations above every exit boundary. |

These absolute values are intentionally easy to reason about and leave room for 1 GiB helper steps. They are not promoted defaults. The first sweep must measure whether they preserve interactivity, avoid OOM or allocation failure, and reclaim too early or too late. The accepted values should then be frozen in the Hatchery catalog rule and evidence manifest.

Using the same 16/8/24 GiB sweep coordinates for GTT and host memory is an experimental convenience, not a claim that the two constraints have equal risk or uncertainty. The physical results may produce different accepted thresholds.

If a controller with these provisional values were active during the live snapshot above, GTT would remain just outside ordinary entry while the roughly 12.23 GiB `MemAvailable` value would enter host-pressure reclamation after dwell. That is intentional: the host floor may dominate even while GTT remains within its utility reserve. The busy snapshot cannot establish that 16 GiB is safe or desirable.

The utility decision that must remain operator-chosen is the preferred 16 GiB entry reserve. The campaign may disprove it as unsafe; only the operator can decide whether a larger safe value is worth earlier reclamation or whether a smaller still-safe value better serves model residency.

## Host watchdog and cgroup evidence

Run both pressure helpers in dedicated cgroup v2 leaves. The kernel documents `memory.current`, `memory.peak`, `memory.high`, `memory.max`, and `memory.events`; `memory.events` includes `high`, `max`, `oom`, and `oom_kill`. `memory.high` throttles rather than invoking the OOM killer, while `memory.max` may invoke the cgroup-local OOM killer if reclaim fails. See [cgroup v2 memory controls](https://docs.kernel.org/admin-guide/cgroup-v2.html#memory-interface-files).

For promotion tests:

- Keep the watchdog outside the Lemonade and helper cgroups.
- Require a controller heartbeat every 250 ms. A one-second heartbeat gap aborts and commands both helpers to release. Control-pipe closure must also make each helper self-release. If a helper has not acknowledged release within 500 ms, the watchdog terminates that helper's cgroup and verifies return to baseline under the 30-second release oracle below.
- Stop increasing host pressure at the provisional 8 GiB critical boundary. A separate watchdog guard at 6 GiB releases controlled helpers immediately, allowing the controller's critical path to be observed while retaining a final 2 GiB experimental buffer.
- Treat any new system or helper-cgroup `oom`, `oom_kill`, unexpected `max`, process death, missed watchdog deadline, or failure to release the helper as a failed attempt.
- Use `memory.high` only to contain the anonymous host-memory helper; do not interpret its expected `high` events as system pressure. Do not assume that cgroup memory controls bound GPU mappings until physical evidence proves the charge path. Do not use `memory.max` as the normal stop mechanism because reaching it can invoke cgroup OOM.
- Record swap counters and PSI. Swap availability does not enlarge `MemAvailable`, the GTT mapping limit, or the predictor's capacity credit.

The 6 GiB watchdog guard is an initial lab parameter. Before physical execution, confirm that the helper's 1 GiB maximum step, release latency, and observed host decline cannot cross it before the watchdog acts. If that cannot be proved, increase the guard and critical boundaries; do not accelerate the test.

## Sampling, skew, and demand envelope

### Two sampling roles

Use separate streams:

| Stream | Initial cadence | Maximum gap | Purpose |
| --- | ---: | ---: | --- |
| Independent campaign oracle | 50 ms | 100 ms | Capture physical peaks, transitions, and helper effects. |
| Candidate runtime controller | 250 ms | 500 ms | Exercise the behavior proposed for the first Hatchery cell. |

The 50 ms oracle cadence matches the kernel PSI monitor's minimum documented update interval for a 500 ms trigger window. It does not imply that PSI and byte counters are synchronized.

For every generation, record monotonic timestamps immediately before and after each source read. The initial maximum cross-source generation skew is 25 ms. A generation that exceeds it is incoherent and cannot authorize action or recovery. Runtime byte evidence becomes stale after 750 ms, three nominal control periods. These are campaign parameters; retain the actual timestamp intervals so the catalog can revise them from evidence.

### Noise and baseline procedure

Do not estimate counter noise from the busy snapshot above. Before each boot epoch:

1. Reconcile Lemonade's ledger and prove that no test-owned backend/helper process remains.
2. Record host-visible DRM clients and all relevant cgroups.
3. Hold a quiescent 15-minute baseline at 50 ms oracle cadence.
4. Reject the baseline if GTT has an unexplained trend, host PSI shows sustained contention, or an unattributed process changes the domain beyond the proposed envelope.
5. Freeze `N_gtt` and `N_host` from the maximum plateau range plus measured read/skew uncertainty. Do not use a percentile for the safety term.

The initial helpers allocate and release in 1 GiB steps, touch and read back the entire step, and hold each plateau for at least two seconds plus one acknowledged coherent observation. The expected step must dominate the frozen noise envelope. If it does not, increase plateau time or reduce environmental noise rather than hiding it statistically.

### Unattributed demand

For a controlled attempt, define `E(W)` as the largest positive unattributed change observed over the complete detection-plus-action-plus-release-verification horizon `W`, then add the designated helper's maximum permitted step and rate. Start the helper at no more than one 1 GiB step every two seconds. Freeze `W` from measured worst-case behavior before held-out runs.

If unrelated demand exceeds the frozen envelope before the stimulus, the attempt is infrastructure-invalid but retained. If Lemonade or its backend exceeds an owned envelope, the attempt fails. If the designated helper exceeds its declared rate, the attempt is helper-invalid. Production behavior outside this bounded external-demand assumption remains best-effort and must say so explicitly.

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
- prove that each step changes global GTT within the frozen measurement envelope before the helper is accepted as an oracle.

PyTorch carries allocator caching, framework initialization, Python packaging, and model/runtime effects that make it a poor deterministic actuator. Use a pinned PyTorch/ROCm workload later as the required real independent application row, not as the threshold-setting helper.

## Workload partition for the first predictor campaign

Do not choose the partition only from what happens to be downloaded. The accepted v1 predicate excludes embedding, reranking, multimodal/projector, MTP/draft/speculative, vLLM, and auxiliary-model paths. It also fixes one slot, empty custom llama.cpp arguments, and disabled prompt RAM/context-checkpoint caches. See [Profile-free residency estimation](profile-free-residency-estimation.md#v1-configuration-predicate).

The current Hatchery cache contains several useful text-only candidates, but three downloaded names explicitly select MTP and are negative applicability fixtures rather than predictor evidence: `Qwen3.5-4B-MTP-GGUF-UD-Q4_K_XL`, `Qwen3.6-27B-MTP-GGUF-UD-Q4_K_XL`, and `Qwen3.6-35B-A3B-MTP-GGUF-UD-Q4_K_XL`. Embedding/reranking models and `Qwen3.5-0.8B-FP16-vLLM` likewise remain outside this cell.

Freeze the artifact digests and inspect the actual GGUF metadata before assigning a model. Subject to that check, use this initial partition:

| Partition | Candidate | Current state | Role |
| --- | --- | --- | --- |
| Calibration | `Llama-3.2-1B-Instruct-GGUF` | Download required | Small dense/GQA and ordinary quantized baseline. |
| Calibration | `Qwen3.5-4B-heretic-v2-i1-GGUF-Q4_K_M` | Downloaded | Small current-cache architecture and a distinct GGUF producer/encoding. |
| Calibration | `LFM2-8B-A1B-GGUF` | Download required | Hybrid/recurrent applicability, if source dry construction classifies every component. |
| Calibration | `gemma-4-26B-A4B-it-GGUF-Q4_K_XL` | Downloaded | Medium mixture-of-experts geometry. |
| Held-out | `gemma-4-E2B-it-GGUF-Q8_K_XL` | Downloaded | Different size/encoding in the Gemma family; it is not calibration evidence for the same revision. |
| Held-out | `Qwen3-30B-A3B-GGUF` | Download required | Different mixture-of-experts family and quantization. |
| Held-out | `Qwen3.5-27B-GGUF` | Download required | Large dense/current-family geometry without MTP. |
| Held-out | `trohrbaugh-Qwen3.5-122B-A10B-heretic-i1-GGUF-Q4_K_M` | Downloaded | Near-capacity model and distinct artifact producer. |

Run calibration at explicit 8 Ki-token and 32 Ki-token contexts where the source metadata permits them, with fixed `parallel=1`, `batch-size=2048`, and `ubatch-size=512`. Run held-out models at 16 Ki tokens and at the largest 4 Ki-aligned context that the analytic envelope predicts can preserve both provisional critical guards. This prevents a held-out case from being silently omitted merely because its architecture's advertised maximum cannot safely fit on Hatchery. Every model traverses cold load, near-empty context, maximum prompt batch, near-target retained context, one generated-token step beyond the prompt, and verified unload/release.

If GGUF metadata or dry construction puts any candidate outside the v1 predicate, replace it before freezing the split; do not widen the predicate or move it between calibration and held-out after observing promotion results. `Tiny-Test-Model-GGUF` remains a lifecycle/race control, not residual calibration evidence.

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

An attempt is infrastructure-invalid only under a frozen rule: pre-stimulus unrelated demand exceeds its envelope, the independent oracle loses required data, the helper violates its protocol for a non-DUT reason, the manifest cannot be committed, or the operator aborts for an unrelated event. Preserve the bundle and reason. A predictor exceedance, protected eviction, wrong-process action, backend crash, release timeout, stale-evidence action, OOM event, watchdog miss, or residual owned allocation is a failed attempt.

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

Keep H-NPU-01 at synthetic conformance/`modeled` in the initial route. Open physical validation only after a host-visible run proves a bound amdxdna device, accessible `/dev/accel`, `flm validate` ready, at least the required LLM/embedding/audio model types installed, and a clean FLM load/unload baseline. NPU/FLM protection semantics still remain implementation requirements; only the physical capability label is deferred.

## Evidence storage for a sole-maintainer fork

Use a three-tier policy:

1. **Git:** Commit run manifests, catalog/predictor identities, normalized summaries, small sanitized traces/fixtures, failure records, SHA-256 digests, and review/acceptance attestations. This is the durable audit index.
2. **GitHub Release assets:** Put large sanitized `.tar.zst` bundles under content-addressed names in a dedicated evidence release series. GitHub permits up to 1,000 assets per release, each under 2 GiB, with no total release-size or bandwidth limit. See [GitHub release quotas](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases#storage-and-bandwidth-quotas).
3. **Private durable storage, only if needed:** Put unredacted process/driver diagnostics in an encrypted content-addressed store with a second backup. Commit only its digest, retention class, redaction relationship, and access-controlled locator. The operator must choose and authorize this store before collecting sensitive data.

Release assets are administratively replaceable or deletable through GitHub's API, so the committed digest and an independent backup supply tamper/loss detection; do not claim the asset service itself is immutable. See [GitHub's release-asset API](https://docs.github.com/en/rest/releases/assets).

Do not use Actions artifacts as the durable store. Public-repository artifact retention is at most 90 days. See [GitHub Actions artifact retention](https://docs.github.com/en/organizations/managing-organization-settings/configuring-the-retention-period-for-github-actions-artifacts-and-logs-in-your-organization).

Do not make Git LFS the default for raw campaigns. GitHub Free includes 10 GiB storage and 10 GiB monthly bandwidth, and every changed large object consumes a new full object. See [Git LFS billing](https://docs.github.com/en/billing/concepts/product-billing/git-lfs). It remains an option for small, stable fixtures whose clone-time availability is worth that cost.

Prefer collection-time minimization: do not record prompts, API keys, environment secrets, full unrelated process arguments, user files, or arbitrary memory. Sanitize deterministically before publication and bind the sanitized bundle to the original digest. If all promotion evidence can be collected without sensitive content, omit the private tier.

## Concrete recommended answers for Issue 35

1. **Thresholds:** Use derived critical predicates plus operator utility thresholds. Start the controlled sweep at GTT and host entry/critical/exit values of 16/8/24 GiB, a one-second noncritical dwell, and five-second recovery stabilization. Treat all six byte values as provisional until the calibration sweep freezes uncertainty and response envelopes.
2. **Host floor:** Make `MemAvailable` an independent constraint on the same RAM. Add cgroup and watchdog evidence, but never substitute swap, PSI, or a GTT percentage for it. The 16 GiB preferred entry reserve remains operator-chosen.
3. **PSI:** Corroborating/severity evidence for the initial cell; authoritative only that stalls occurred, not for byte capacity or GTT attribution.
4. **Sampling:** Use a 50 ms independent oracle and 250 ms candidate controller, 25 ms maximum generation skew, 750 ms freshness, 1 GiB helper steps held at least two seconds, and a predeclared bounded external-demand rate. Revise these values only through a new frozen campaign manifest.
5. **GTT helper:** Native HIP first; PyTorch only as a later representative external application.
6. **Workload split:** Freeze the four-model calibration and four-model held-out partition above only after artifact/GGUF inspection. Keep MTP and other excluded shapes as negative applicability fixtures and lifecycle controls out of residual evidence.
7. **Cold attempts:** Three runtime-cold attempts per physical row across three boot-cold epochs, with sentinel rows repeated each boot. Treat owned leaks and controller/backend failures as failures, never infrastructure invalidations.
8. **NPU/FLM:** Retain synthetic conformance and `modeled`; physical promotion is blocked by the presently disabled/unbound NPU and empty FLM model store.
9. **Evidence:** Git index plus sanitized GitHub Release assets, Actions only for transit, and an operator-selected encrypted private store only if collection cannot avoid sensitive diagnostics.

## Operator choices that remain

The research does not settle these value judgments:

1. Is preserving 16 GiB of ordinary GTT and host headroom the desired Hatchery utility tradeoff, subject to the campaign proving it safe, or should the experiment start with a larger reserve?
2. Is a five-second recovery stabilization acceptable, or is avoiding repeated unload/reload more important than faster recovery exit?
3. May sanitized physical evidence be public in `nisavid/lemonade` Release assets?
4. If unredacted evidence is unavoidable, which encrypted private store and retention period are authorized?

All other recommendations above can be accepted as the initial campaign contract and revised only if physical evidence contradicts them.
