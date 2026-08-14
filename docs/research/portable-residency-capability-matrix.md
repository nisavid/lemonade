# Portable residency capability matrix

Status: accepted Issue [#35](https://github.com/nisavid/lemonade/issues/35) conceptual contract; not proof of implementation, catalog publication, or physical validation.

Date: 2026-08-13

## Scope baseline

This matrix is frozen against upstream Lemonade `main` commit [`a505bbc702cc1fcd44ef73c44defabc98c36d505`](https://github.com/lemonade-sdk/lemonade/commit/a505bbc702cc1fcd44ef73c44defabc98c36d505). It covers every residency-relevant GPU and NPU recipe/backend support row declared by that source. A later upstream support change creates a new matrix revision; a new backend, operating system, accelerator family, topology, model type, or memory-affecting configuration never inherits authority from a similarly named row.

GPU model residency and NPU/FLM compatibility are in scope. CPU-only backends, remote `cloud`, and collection recipes are not independent residency cells:

- Dedicated CPU rows and CPU-resolved Vulkan rows remain on their existing compatibility paths. This includes `llamacpp`, `whispercpp`, `sd-cpp`, `kokoro`, `moonshine`, `onnxruntime`, `thinksound`, `acestep`, `trellis`, and `openmoss` where their frozen descriptor selects CPU. These paths retain model-type-pool, ownership, and verified-release obligations but gain no memory-capacity automation.
- `cloud` owns no local model-residency resource.
- `collection.omni` and `collection.router` resolve into their concrete child recipes before cell selection.
- `llamacpp:system` must attest and resolve one exact installed implementation and dependency closure. An ambiguous or CPU result selects the excluded compatibility path; an exact GPU result matches only the corresponding concrete GPU cell.

The accepted [Hatchery validation profile](hatchery-residency-validation-profile.md), [campaign parameters](hatchery-campaign-parameters.md), and [profile-free estimator](profile-free-residency-estimation.md) are the first physical program. They grant no authority to another cell.

## Cell identity and state

One runtime cell is the unique match over this complete key:

```text
support_baseline
× operation_leaf
× canonical_affected_constraint_set
× platform_provider / OS / hardware / topology / durable_device_identities
× recipe / concrete_backend / artifact_and_dependency_closure
× model_type / normalized_configuration / workload_applicability
× predictor_rule / observation_contract
× recovery_authority_mode
```

Predicates must be disjoint or use one generated, reviewed most-specific precedence relation. No unique match means an incoherent catalog and refusal before a resource effect. Display labels such as “Linux AMD,” “llama.cpp,” or “validated ROCm” are never policy keys.

An evidence-only compatibility contract is not a runtime cell. Its separate identity binds the frozen source baseline, exact source-derived cross-variant cases, directions, incumbent states, model-type coverage, `NPC` relation selector, enumerated relation constraints, evidence and gate sets, state, and fallback. It may justify only those enumerated relation constraints; it has no hardware, runtime, process, claim, or recovery identity and grants no live action authority. Participant-local slot cardinality, ownership, protection, cleanup, claim replay, and recovery remain unproved until their separate exact promotion units pass. Its `delivery_state` gates availability, so synthetic evidence cannot make an absent contract usable. Physical qualification materializes separate exact runtime cells for each platform case.

Every generated cell record contains:

- `cell_id`, schema version, source-support baseline, and content digest;
- the full match key above and the canonical affected-constraint set;
- complete claim-manifest families and minimum footprint confidence;
- required observations, identities, freshness, skew, uncertainty, health, and revalidation boundaries;
- recovery-authority mode, action mechanism, and independently verified release rule;
- `evidence_ceiling`, accepted `capability_level`, and `delivery_state`;
- typed configured intent and operation-specific effective-mode vocabulary;
- a stable concrete `fallback_id`, behavior, and generated reason;
- conjunctive promotion-suite IDs, evidence-manifest digest, liveness expiry, and inheritance prohibition.

Four state dimensions remain separate:

| Dimension | Closed meaning |
| --- | --- |
| `evidence_ceiling` | Strongest capability a frozen evidence program is allowed to justify for this promotion unit. A planned ceiling is not a result. |
| `capability_level` | `unsupported`, `fallback_only`, `modeled`, or `validated` actually accepted by one reviewed, immutable server-owned catalog revision. |
| `delivery_state` | `absent`, `implemented_unverified`, or `release_verified` for the complete runtime-cell or compatibility-contract implementation. Partial source remains `absent`; complete release-artifact verification may set `release_verified` regardless of evidence method, without changing `capability_level` or `evidence_ceiling`. |
| Live authority | Applicable current signal health, evidence-liveness lease, topology/configuration generations, and recovery readiness. It changes effective behavior, not catalog history; an evidence-only contract with `runtime_authority=none` has no live action authority. |

The effective mode is the conservative meet of configured intent, delivery state, accepted capability level, evidence-liveness authority, live observation health, and recovery readiness. No adapter or manifest self-promotes. The design baseline recorded here is `capability_level=unsupported` and `delivery_state=absent` for every target runtime cell and compatibility contract because the complete portable control plane and catalog are not implemented at this fork revision. Legacy loading, LRU, pin, and retry behavior remains current compatibility behavior, not partial evidence for a target promotion unit.

## Canonical affected constraints

A cell uses a canonical set assembled from these families:

| Constraint family | Required interpretation |
| --- | --- |
| `gpu_provider_resolved_capacity` | Provider-qualified signed per-device capacity, such as physical VRAM. Multi-device use is a vector, never one summed budget. |
| `host_effects_provider_resolved` | Provider-qualified host effects paired with discrete or otherwise provider-resolved GPU capacity; a reviewed cell must bound or explicitly prove every component. |
| `gpu_shared_residency` | GTT or another reviewed shared/unified residency domain. One allocation group may project into this domain and host memory without becoming two allocations. |
| `host_memavailable_floor` | Independent host safety floor for shared GPU memory, backend heaps, staging, and other cataloged host effects. It is not added to GPU capacity. |
| `model_type_pool` | Existing per-model-type cardinality ceiling, including LLM, embedding, reranking, transcription, image, TTS, audio generation, classification, and mesh. |
| `npu_exclusive` | Exclusive XDNA2 holder used by `ryzenai-llm` and `whispercpp:npu`. |
| `flm_type_slot` | At most one FLM resident per FLM model type where upstream permits coexistence. |
| `npu_cross_family` | FLM versus exclusive-NPU compatibility boundary. |
| `ownership` | Complete process, resource, action-lease, and recovery ownership required for safe effects and verified release. |

Action leases and process/resource ownership claims are mandatory in every applicable completeness manifest. They are not extra physical memory domains or new ordinary claim kinds.

## Operation templates

Every concrete backend support row expands into the applicable operation templates below. `not_applicable` is legal only when source and protocol evidence proves that the backend cannot own that lifecycle shape.

| Template | Covered operation leaves | Required behavior | Concrete fallback when authority is insufficient |
| --- | --- | --- | --- |
| `ADM` | `admission`; discovery/candidate union; request transient and retained growth; ordinary load | Reserve the complete lifetime envelope and all non-memory claims before the first effect. Runtime realization may narrow only. | `residency_admission_refuse_unknown_demand_v1`: refuse before victim selection, reclamation, discovery spawn, or backend spawn. |
| `LFR` | load-failure retry subcase of `admission` | Classify the failure, reserve one complete immutable retry plan, protect pin/use leases, and retry only after verified release. | `residency_load_retry_refuse_unproven_victim_set_v1`: preserve residents and return the original/registered failure; never use an evict-all retry. |
| `PRE` | `pressure_reclamation` | Observe measured pressure, plan the full deficit vector, perform only cataloged soft/hard actions, and verify release before credit. | Valid reporting only: `residency_pressure_report_only_unvalidated_v1`. Invalid reporting evidence: `residency_pressure_disabled_invalid_evidence_v1`. Both preserve residency and perform no automatic soft or hard mutation. |
| `STA` | `startup_load` | Resolve connected saved-preference groups, reserve each whole group, load provisionally, then commit or roll back atomically. | `residency_startup_block_group_v1`: load none of the connected group and preserve every saved preference. |
| `REC` | `service_termination`; `dead_backend_pruning`; `same_epoch_recovery_cleanup`; `prior_epoch_owner_cleanup`; `artifact_scope_recovery_cleanup` | Fence ownership, replay maximum claims, perform cleanup only under bound authority, quarantine ambiguity, and verify complete release. | `residency_recovery_block_unproven_release_v1`: keep lifecycle unavailable and retain maximum plausible claims. |
| `UNL` | `explicit_unload`; `force_unload` | Explicit unload may ignore pin but waits for live use. Force unload first fences the exact use generation. Both transfer every effect atomically. | `residency_unload_preserve_live_use_v1`: refuse or wait without signaling an unproven owner. |
| `PIN` | `saved_pin_mutation`; `runtime_pin_mutation`; `legacy_pin_batch`; `resident_state_recovery_cleanup` | Use the lifecycle gate, conditional generations where required, and the safety-reducing reserve for exact unpin effects. | `residency_pin_mutation_refuse_stale_or_unready_v1`: preserve saved/runtime state and return the generated precondition/readiness result. |
| `NPC` | Evidence-only NPU/FLM compatibility relation selector for `admission` | Prove only the closed cross-family relation cases and their fail-closed response; automatic displacement requires separate exact conflict, protection, action, and release cells. | `residency_npu_conflict_preserve_refuse_v1`: preserve every incumbent, including unpinned-idle residents, and refuse the newcomer. |

Runtime cells use `ADM`, `LFR`, `PRE`, `STA`, `REC`, `UNL`, and `PIN` as applicable. `NPC` is not a runtime-cell leaf: evidence-only NPU compatibility contracts use it solely to identify their closed relation cases and preserve-incumbent response. Generic NPU runtime `ADM` refuses unknown byte demand, and participant-local `REC`, `UNL`, `PIN`, ownership, and release remain obligations of separate exact runtime cells. `PRE` is `not_applicable` until a separate NPU capacity/pressure contract exists. Artifact writers and delete coordinators are not residency operations, but every backend family must pass their cross-cutting quarantine and recovery handoff fixtures when those surfaces can affect its model identity or artifacts.

## Upstream support inventory and generated cell groups

The machine-readable [portable support inventory](portable-residency-capability-inventory.json) is authoritative for backend variants, finite platform predicates, source-derived model types, provider/topology rules, constraint/operation/recovery/suite references, atomic promotion gates, evidence ceilings, exact runtime cells, evidence-only compatibility contracts, and exclusions. It binds the complete descriptor/parser and model-type input closure at the frozen upstream commit. `tools/validate_residency_capability_inventory.py` derives upstream support from `LEMON_BACKENDS` and each descriptor, requires every support row to map exactly once to a target variant or explicit exclusion, validates source-derived model types and the closed promotion/gate/fallback registries, and byte-checks the generated projections.

The validator requires the exact pinned commit object, every path/blob object named by `source_file_blobs`, and every tree object named by `source_tree_objects`; no branch tip or remote-tracking ref is an authority input. Fetch the immutable object directly from the public upstream URL, then validate the declared object closure:

```bash
git fetch --no-tags --depth=1 https://github.com/lemonade-sdk/lemonade.git a505bbc702cc1fcd44ef73c44defabc98c36d505
git cat-file -e 'a505bbc702cc1fcd44ef73c44defabc98c36d505^{commit}'
python3 tools/validate_residency_capability_inventory.py
```

If a local remote is already configured to fetch that public repository, `git fetch --no-tags --depth=1 <remote-name> a505bbc702cc1fcd44ef73c44defabc98c36d505` is an equivalent transport alternative. The procedure does not require a remote named `upstream`. Fetching supplies objects only; the validator still compares the exact commit SHA and every declared source-path blob and tree identity from the inventory before deriving support.

Material Markdown bindings use canonical UTF-8 text with CRLF and CR translated to LF before SHA-256. A `path#heading` binding includes the selected heading line and its section through, but not including, the next heading of the same or higher level. The complete campaign-base binding uses the same canonical-text convention. This keeps content identity stable across POSIX and Windows checkouts while making heading changes material.

The `--update` path is a locked, crash-consistent and recoverable two-document update of the matrix and campaign projections. Validator and render clients share the inventory lock and refuse pending journals. Raw readers do not participate in that lock, so they may briefly observe mixed projections between the two file replacements; the update does not provide atomic cross-file visibility. The next `--update` recovers a pending transaction before completing the requested update.

The table below is a generated human projection, not a second source of cell applicability.

<!-- BEGIN GENERATED SUPPORT INVENTORY -->
### Frozen source closure

| Field | Identity |
| --- | --- |
| Support baseline | `a505bbc702cc1fcd44ef73c44defabc98c36d505` |
| C++ source tree `src/cpp` | `c242d0c885eb78ee62b38a25da8a17abb65a01fa` |

### Backend variants

| Variant ID | Recipe/backend | Platform predicates | Model types | Constraint profile | Operation set | Recovery profile | Evidence ceiling |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `llamacpp-metal` | `llamacpp:metal` | `macos-metal-apple-silicon` | `llm`, `embedding`, `reranking` | `apple_unified` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `llamacpp-cuda` | `llamacpp:cuda` | `linux-nvidia-cuda-listed`<br>`windows-nvidia-cuda-listed` | `llm`, `embedding`, `reranking` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `llamacpp-vulkan` | `llamacpp:vulkan` | `linux-amd-vulkan`<br>`windows-amd-vulkan` | `llm`, `embedding`, `reranking` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `llamacpp-rocm` | `llamacpp:rocm` | `linux-amd-rocm-llamacpp`<br>`linux-amd-rocm-llamacpp-gfx950-stable`<br>`windows-amd-rocm-llamacpp` | `llm`, `embedding`, `reranking` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `whispercpp-metal` | `whispercpp:metal` | `macos-metal-apple-silicon` | `transcription` | `apple_unified` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `whispercpp-vulkan` | `whispercpp:vulkan` | `linux-amd-vulkan`<br>`windows-amd-vulkan` | `transcription` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `whispercpp-rocm` | `whispercpp:rocm` | `linux-amd-rocm-whisper`<br>`windows-amd-rocm-whisper` | `transcription` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `whispercpp-npu` | `whispercpp:npu` | `windows-xdna2` | `transcription` | `npu_exclusive` | `npu_compatibility` | `native_subprocess_tree` | `modeled` |
| `sd-cpp-metal` | `sd-cpp:metal` | `macos-metal-apple-silicon` | `image` | `apple_unified` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `sd-cpp-cuda` | `sd-cpp:cuda` | `linux-nvidia-cuda-listed`<br>`windows-nvidia-cuda-listed` | `image` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `sd-cpp-vulkan` | `sd-cpp:vulkan` | `linux-amd-vulkan`<br>`windows-amd-vulkan`<br>`linux-nvidia-vulkan`<br>`windows-nvidia-vulkan` | `image` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `sd-cpp-rocm` | `sd-cpp:rocm` | `linux-amd-rocm-standard`<br>`windows-amd-rocm-standard` | `image` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `kokoro-metal` | `kokoro:metal` | `macos-metal-apple-silicon` | `tts` | `apple_unified` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `vllm-rocm` | `vllm:rocm` | `linux-amd-rocm-vllm` | `llm` | `provider_resolved_gpu` | `gpu_resource` | `python_or_async_job_tree` | `modeled` |
| `thenoise-rocm` | `thenoise:rocm` | `linux-amd-rocm-thenoise` | `image` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `thinksound-cuda` | `thinksound:cuda` | `linux-nvidia-cuda-any`<br>`windows-nvidia-cuda-any` | `audio-generation` | `provider_resolved_gpu` | `gpu_resource` | `python_or_async_job_tree` | `modeled` |
| `thinksound-vulkan` | `thinksound:vulkan` | `linux-amd-vulkan`<br>`windows-amd-vulkan`<br>`linux-nvidia-vulkan`<br>`windows-nvidia-vulkan` | `audio-generation` | `provider_resolved_gpu` | `gpu_resource` | `python_or_async_job_tree` | `modeled` |
| `thinksound-rocm` | `thinksound:rocm` | `linux-amd-rocm-standard`<br>`windows-amd-rocm-standard` | `audio-generation` | `provider_resolved_gpu` | `gpu_resource` | `python_or_async_job_tree` | `modeled` |
| `acestep-cuda` | `acestep:cuda` | `linux-nvidia-cuda-any`<br>`windows-nvidia-cuda-any` | `audio-generation` | `provider_resolved_gpu` | `gpu_resource` | `python_or_async_job_tree` | `modeled` |
| `acestep-vulkan` | `acestep:vulkan` | `linux-amd-vulkan`<br>`windows-amd-vulkan`<br>`linux-nvidia-vulkan`<br>`windows-nvidia-vulkan` | `audio-generation` | `provider_resolved_gpu` | `gpu_resource` | `python_or_async_job_tree` | `modeled` |
| `acestep-rocm` | `acestep:rocm` | `linux-amd-rocm-standard`<br>`windows-amd-rocm-standard` | `audio-generation` | `provider_resolved_gpu` | `gpu_resource` | `python_or_async_job_tree` | `modeled` |
| `trellis-cuda` | `trellis:cuda` | `linux-nvidia-cuda-any`<br>`windows-nvidia-cuda-any` | `mesh` | `provider_resolved_gpu` | `gpu_resource` | `python_or_async_job_tree` | `modeled` |
| `trellis-vulkan` | `trellis:vulkan` | `linux-amd-vulkan`<br>`windows-amd-vulkan`<br>`linux-nvidia-vulkan`<br>`windows-nvidia-vulkan` | `mesh` | `provider_resolved_gpu` | `gpu_resource` | `python_or_async_job_tree` | `modeled` |
| `trellis-rocm` | `trellis:rocm` | `linux-amd-rocm-standard`<br>`windows-amd-rocm-standard` | `mesh` | `provider_resolved_gpu` | `gpu_resource` | `python_or_async_job_tree` | `modeled` |
| `openmoss-cuda` | `openmoss:cuda` | `linux-nvidia-cuda-any`<br>`windows-nvidia-cuda-any` | `tts` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `openmoss-vulkan` | `openmoss:vulkan` | `linux-amd-vulkan`<br>`windows-amd-vulkan`<br>`linux-nvidia-vulkan`<br>`windows-nvidia-vulkan` | `tts` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `openmoss-rocm` | `openmoss:rocm` | `linux-amd-rocm-open`<br>`windows-amd-rocm-open` | `tts` | `provider_resolved_gpu` | `gpu_resource` | `native_subprocess_tree` | `modeled` |
| `flm-npu` | `flm:npu` | `linux-xdna2`<br>`windows-xdna2` | `llm`, `embedding`, `transcription` | `npu_flm` | `npu_compatibility` | `flm_system_managed` | `modeled` |
| `ryzenai-llm-npu` | `ryzenai-llm:npu` | `windows-xdna2` | `llm` | `npu_exclusive` | `npu_compatibility` | `native_subprocess_tree` | `modeled` |

### Source-support exclusions

| Exclusion ID | Frozen source-support dispositions | Empty-support recipes | Non-descriptor recipes |
| --- | --- | --- | --- |
| `cpu-residency` | `llamacpp:cpu/linux/cpu/arm64/single`<br>`llamacpp:cpu/linux/cpu/x86_64/single`<br>`llamacpp:cpu/windows/cpu/arm64/single`<br>`llamacpp:cpu/windows/cpu/x86_64/single`<br>`whispercpp:cpu/linux/cpu/x86_64/single`<br>`whispercpp:cpu/windows/cpu/x86_64/single`<br>`sd-cpp:cpu/linux/cpu/x86_64/single`<br>`sd-cpp:cpu/windows/cpu/x86_64/single`<br>`kokoro:cpu/linux/cpu/x86_64/single`<br>`kokoro:cpu/windows/cpu/x86_64/single`<br>`moonshine:cpu/windows/cpu/x86_64/single`<br>`moonshine:cpu/linux/cpu/arm64/single`<br>`moonshine:cpu/linux/cpu/x86_64/single`<br>`moonshine:cpu/macos/cpu/arm64/single`<br>`onnxruntime:cpu/windows/cpu/x86_64/single`<br>`onnxruntime:cpu/linux/cpu/arm64/single`<br>`onnxruntime:cpu/linux/cpu/x86_64/single`<br>`onnxruntime:cpu/macos/cpu/arm64/single` | — | — |
| `cpu-vulkan-residency` | `llamacpp:vulkan/linux/cpu/arm64/single`<br>`llamacpp:vulkan/linux/cpu/x86_64/single`<br>`llamacpp:vulkan/windows/cpu/arm64/single`<br>`llamacpp:vulkan/windows/cpu/x86_64/single`<br>`whispercpp:vulkan/linux/cpu/x86_64/single`<br>`whispercpp:vulkan/windows/cpu/x86_64/single`<br>`sd-cpp:vulkan/linux/cpu/x86_64/single`<br>`sd-cpp:vulkan/windows/cpu/x86_64/single`<br>`thinksound:vulkan/linux/cpu/x86_64/single`<br>`thinksound:vulkan/windows/cpu/x86_64/single`<br>`acestep:vulkan/linux/cpu/x86_64/single`<br>`acestep:vulkan/windows/cpu/x86_64/single`<br>`trellis:vulkan/linux/cpu/x86_64/single`<br>`trellis:vulkan/windows/cpu/x86_64/single`<br>`openmoss:vulkan/linux/cpu/x86_64/single`<br>`openmoss:vulkan/windows/cpu/x86_64/single` | — | — |
| `llamacpp-system-ambiguous` | `llamacpp:system/linux/cpu/arm64/single`<br>`llamacpp:system/linux/cpu/x86_64/single` | — | — |
| `remote-cloud` | — | `cloud` | — |
| `collections` | — | — | `collection.omni`, `collection.router` |

### Material profile semantic identities

| Registry | Profile ID | Accepted non-document semantics |
| --- | --- | --- |
| `configuration_profiles` | `profile-free-residency-estimation-v1-text-only` | `model_types` = `["llm"]` |
| `hardware_profiles` | `hatchery-gfx1151-shared-gtt-v1` | `base_topology` = `provider_resolved`<br>`required_runtime_bindings` = `["device_identity","driver_runtime_closure"]`<br>`source_support` = `{"accelerator":"amd_gpu","architectures":["gfx1151"],"backend":"rocm","channels":["stable"],"os":"linux","recipe":"llamacpp"}`<br>`topology` = `shared_gtt` |
| `observation_contracts` | `hatchery-gtt-host-observation-v1` | `constraints` = `["gpu_shared_residency","host_memavailable_floor"]` |
| `predictor_rules` | `hatchery-llamacpp-rocm-profile-free-v1` | `confidence_target` = `validated_predictor` |
| `workload_profiles` | `hatchery-text-generation-campaign-v1` | — |

### Material profile document bindings

| Binding | Locator | SHA-256 |
| --- | --- | --- |
| `configuration_profiles.profile-free-residency-estimation-v1-text-only.document` | `docs/research/profile-free-residency-estimation.md#v1-configuration-predicate` | `fb8da2962ff9070cc172995806f81ccf6c1ada267bd75f4edab9a29d7fb3cdda` |
| `hardware_profiles.hatchery-gfx1151-shared-gtt-v1.evidence_document` | `docs/research/hatchery-campaign-parameters.md#live-hatchery-observations` | `c43642e3ef985ee02d2e2e42d6a8ee8ac3e6f40703b88de97931fb91b43e501f` |
| `observation_contracts.hatchery-gtt-host-observation-v1.document` | `docs/research/hatchery-campaign-parameters.md#live-hatchery-observations` | `c43642e3ef985ee02d2e2e42d6a8ee8ac3e6f40703b88de97931fb91b43e501f` |
| `predictor_rules.hatchery-llamacpp-rocm-profile-free-v1.document` | `docs/research/profile-free-residency-estimation.md#validation-and-promotion-threshold` | `a3184cf7a6c3e436ce41e11dee30ca1c7c780ac01374125d3880f6c46f46aef9` |
| `workload_profiles.hatchery-text-generation-campaign-v1.document` | `docs/research/hatchery-campaign-parameters.md#workload-partition-for-the-first-predictor-campaign` | `27840de0b38f113c13edcee700f4c27a7916a8d6888e10d966ffe39b11124aea` |

### Coverage policy

| Field | Accepted value |
| --- | --- |
| `classification_disposition` | `exclude_cpu_onnxruntime_compatibility` |
| `host_floor_rule` | `constraint_profile_only` |
| `multi_device_rule` | `separate_exact_topology_placement_cell` |

### Validation suite registry

| Suite ID | Operations | Proof |
| --- | --- | --- |
| `PT-ID` | `*` | identity, provenance, and immutable fingerprint |
| `PT-TOP` | `*` | topology and placement attribution |
| `PT-FP` | `ADM`, `STA` | complete conservative lifetime footprint |
| `PT-SIG` | `ADM`, `PRE`, `STA` | fresh coherent signals and uncertainty |
| `PT-ADM` | `ADM` | predictive admission and growth |
| `PT-LFR` | `LFR` | load-failure retry without unproved victims |
| `PT-PRE` | `PRE` | pressure reclamation and verified recovery |
| `PT-STA` | `STA` | grouped startup admission |
| `PT-NPU` | `ADM`, `NPC` | NPU and FLM compatibility admission and conflict |
| `PT-REC` | `REC`, `NPC` | runtime ownership replay and verified release, or compatibility-synthetic fail-closed relation-state response |
| `PT-UNL` | `UNL` | explicit and forced unload protection |
| `PT-PIN` | `PIN` | saved and runtime pin mutation |
| `PT-CON` | `*` | atomic concurrency and stale-token rejection |
| `PT-ART` | `*` | artifact and native-writer ambiguity handoff |
| `PT-EXP` | `*` | generated explanation and reason conformance |
| `PT-LIV` | `*` | evidence-liveness rollback and restoration |

### Validation suite sets

| Suite set | Suites |
| --- | --- |
| `gpu_standard` | `PT-ID`, `PT-TOP`, `PT-FP`, `PT-SIG`, `PT-ADM`, `PT-LFR`, `PT-PRE`, `PT-STA`, `PT-REC`, `PT-UNL`, `PT-PIN`, `PT-CON`, `PT-ART`, `PT-EXP`, `PT-LIV` |
| `npu_compatibility` | `PT-ID`, `PT-TOP`, `PT-SIG`, `PT-LFR`, `PT-STA`, `PT-NPU`, `PT-REC`, `PT-UNL`, `PT-PIN`, `PT-CON`, `PT-EXP`, `PT-LIV` |

### Campaign base binding

| Binding | Document | SHA-256 |
| --- | --- | --- |
| Campaign base profile | `docs/research/hatchery-residency-validation-profile.md` | `ee6d0bd0586a2e448afd915573f697abdbdc1880fcd495c1fcbfc92cedf0d94c` |

### Gate source bindings

| Source ID | Document | Section | Section SHA-256 |
| --- | --- | --- | --- |
| `hatchery_base` | `docs/research/hatchery-residency-validation-profile.md` | Accepted Hatchery validation matrix | `8fe930baadc1fe0d0d4f383750d10fbd89d9287bcc62066d265e79ec1a3b7d82` |
| `hatchery_overlay` | `docs/research/hatchery-campaign-parameters.md` | Atomic overlay gate definitions | `51c30f1931f85ae0faf3b08c6651174fcd2b024478af015532aa5082d14a320a` |

### Atomic campaign gate registry

| Gate ID | Source | Applications | Suites |
| --- | --- | --- | --- |
| `H-TOP-01` | `hatchery_base` | `exact_runtime` | `PT-ID`, `PT-TOP` |
| `H-FP-01` | `hatchery_base` | `exact_runtime` | `PT-FP` |
| `H-ADM-01` | `hatchery_base` | `exact_runtime` | `PT-ADM` |
| `H-ADM-02` | `hatchery_base` | `exact_runtime` | `PT-ADM` |
| `H-ADM-03` | `hatchery_base` | `exact_runtime`, `exact_synthetic` | `PT-ADM` |
| `H-ADM-04` | `hatchery_base` | `exact_runtime` | `PT-ADM` |
| `H-GROW-01` | `hatchery_base` | `exact_runtime` | `PT-ADM` |
| `H-PRE-01` | `hatchery_base` | `exact_runtime` | `PT-PRE` |
| `H-PRE-02` | `hatchery_base` | `exact_runtime` | `PT-PRE` |
| `H-PRE-03` | `hatchery_base` | `exact_runtime` | `PT-PRE` |
| `H-PRE-04` | `hatchery_base` | `exact_runtime` | `PT-PRE` |
| `H-PROT-01` | `hatchery_base` | `exact_runtime` | `PT-ADM`, `PT-PRE` |
| `H-PROT-02` | `hatchery_base` | `exact_runtime` | `PT-ADM`, `PT-PRE` |
| `H-ORD-01` | `hatchery_base` | `exact_runtime`, `exact_synthetic` | `PT-ADM`, `PT-PRE` |
| `H-CON-01` | `hatchery_base` | `exact_runtime`, `exact_synthetic` | `PT-CON` |
| `H-CON-02` | `hatchery_base` | `exact_runtime`, `exact_synthetic` | `PT-CON` |
| `H-EVD-01` | `hatchery_base` | `exact_runtime`, `exact_synthetic`, `compatibility_synthetic` | `PT-SIG` |
| `H-STA-01` | `hatchery_base` | `exact_runtime` | `PT-STA` |
| `H-NPU-01` | `hatchery_base` | `compatibility_synthetic` | `PT-NPU` |
| `H-REC-01` | `hatchery_base` | `exact_runtime`, `exact_synthetic` | `PT-REC`, `PT-ART` |
| `H-EXP-01` | `hatchery_base` | `exact_runtime`, `exact_synthetic`, `compatibility_synthetic` | `PT-EXP` |
| `H-EXT-01` | `hatchery_overlay` | `exact_runtime` | `PT-PRE` |
| `H-NPU-TOP-01` | `hatchery_overlay` | `compatibility_synthetic` | `PT-ID`, `PT-TOP` |
| `H-NPU-PROT-01` | `hatchery_overlay` | `compatibility_synthetic` | `PT-NPU` |
| `H-NPU-PROT-02` | `hatchery_overlay` | `compatibility_synthetic` | `PT-NPU` |
| `H-NPU-CON-01` | `hatchery_overlay` | `compatibility_synthetic` | `PT-CON` |
| `H-NPU-CON-02` | `hatchery_overlay` | `compatibility_synthetic` | `PT-CON` |
| `H-NPU-REC-01` | `hatchery_overlay` | `compatibility_synthetic` | `PT-REC` |
| `H-LIV-01a` | `hatchery_overlay` | `exact_runtime`, `exact_synthetic`, `compatibility_synthetic` | `PT-LIV` |
| `H-LIV-01b` | `hatchery_overlay` | `exact_runtime`, `exact_synthetic`, `compatibility_synthetic` | `PT-LIV` |
| `H-LIV-01c` | `hatchery_overlay` | `exact_runtime`, `exact_synthetic`, `compatibility_synthetic` | `PT-LIV` |
| `H-LIV-01d` | `hatchery_overlay` | `exact_runtime`, `exact_synthetic`, `compatibility_synthetic` | `PT-LIV` |
| `H-LIV-01e` | `hatchery_overlay` | `exact_runtime`, `exact_synthetic`, `compatibility_synthetic` | `PT-LIV` |
| `H-LIV-01f` | `hatchery_overlay` | `exact_runtime`, `exact_synthetic`, `compatibility_synthetic` | `PT-LIV` |
| `H-LIV-01g` | `hatchery_overlay` | `exact_runtime`, `exact_synthetic`, `compatibility_synthetic` | `PT-LIV` |

### Flattened campaign gate sets

| Gate set | Atomic gates |
| --- | --- |
| `evidence_common_v1` | `H-EXP-01`, `H-LIV-01a`, `H-LIV-01b`, `H-LIV-01c`, `H-LIV-01d`, `H-LIV-01e`, `H-LIV-01f`, `H-LIV-01g` |
| `hatchery_common_v1` | `H-EXP-01`, `H-LIV-01a`, `H-LIV-01b`, `H-LIV-01c`, `H-LIV-01d`, `H-LIV-01e`, `H-LIV-01f`, `H-LIV-01g`, `H-TOP-01` |
| `hatchery_rocm_adm_v1` | `H-EXP-01`, `H-LIV-01a`, `H-LIV-01b`, `H-LIV-01c`, `H-LIV-01d`, `H-LIV-01e`, `H-LIV-01f`, `H-LIV-01g`, `H-TOP-01`, `H-FP-01`, `H-ADM-01`, `H-ADM-02`, `H-ADM-03`, `H-ADM-04`, `H-GROW-01`, `H-PROT-01`, `H-PROT-02`, `H-ORD-01`, `H-CON-01`, `H-CON-02`, `H-EVD-01`, `H-REC-01` |
| `hatchery_rocm_pre_v1` | `H-EXP-01`, `H-LIV-01a`, `H-LIV-01b`, `H-LIV-01c`, `H-LIV-01d`, `H-LIV-01e`, `H-LIV-01f`, `H-LIV-01g`, `H-TOP-01`, `H-FP-01`, `H-PRE-01`, `H-PRE-02`, `H-PRE-03`, `H-PRE-04`, `H-EXT-01`, `H-PROT-01`, `H-PROT-02`, `H-ORD-01`, `H-CON-02`, `H-EVD-01`, `H-REC-01` |
| `hatchery_rocm_sta_v1` | `H-EXP-01`, `H-LIV-01a`, `H-LIV-01b`, `H-LIV-01c`, `H-LIV-01d`, `H-LIV-01e`, `H-LIV-01f`, `H-LIV-01g`, `H-TOP-01`, `H-FP-01`, `H-STA-01`, `H-CON-01`, `H-CON-02`, `H-EVD-01`, `H-REC-01` |
| `hatchery_rocm_rec_v1` | `H-EXP-01`, `H-LIV-01a`, `H-LIV-01b`, `H-LIV-01c`, `H-LIV-01d`, `H-LIV-01e`, `H-LIV-01f`, `H-LIV-01g`, `H-TOP-01`, `H-EVD-01`, `H-CON-02`, `H-REC-01` |
| `xdna2_npu_flm_conflict_v1` | `H-EXP-01`, `H-LIV-01a`, `H-LIV-01b`, `H-LIV-01c`, `H-LIV-01d`, `H-LIV-01e`, `H-LIV-01f`, `H-LIV-01g`, `H-NPU-TOP-01`, `H-NPU-01`, `H-NPU-PROT-01`, `H-NPU-PROT-02`, `H-NPU-CON-01`, `H-NPU-CON-02`, `H-EVD-01`, `H-NPU-REC-01` |

### Exact promoted-cell selectors

| Cell ID | Base variant / platform | Operation | Scope | Model types | Constraints | Evidence ceiling / current state | Promotion target | Campaign gate set | Fallbacks |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `H-ROCM-ADM-GTT-HOST-v1` | `llamacpp-rocm` / `linux-amd-rocm-llamacpp` / `stable` | `ADM` / `admission` | Predictive admission and lifetime growth over canonical GTT headroom plus the host-MemAvailable floor | `llm` | `gpu_shared_residency`, `host_memavailable_floor`, `model_type_pool`, `ownership` | `validated` / `unsupported` / `absent` | validated, with separately catalog-assigned validated_predictor confidence for the complete manifest | `hatchery_rocm_adm_v1` | `insufficient_capacity_authority` → `hatchery_rocm_admission_refuse_unknown_capacity_v1` |
| `H-ROCM-PRE-GTT-HOST-v1` | `llamacpp-rocm` / `linux-amd-rocm-llamacpp` / `stable` | `PRE` / `pressure_reclamation` | Measured-pressure reclamation over canonical GTT headroom plus the host-MemAvailable floor | `llm` | `gpu_shared_residency`, `host_memavailable_floor`, `model_type_pool`, `ownership` | `validated` / `unsupported` / `absent` | validated | `hatchery_rocm_pre_v1` | `invalid_reporting_evidence` → `hatchery_rocm_pressure_disabled_invalid_evidence_v1`<br>`valid_reporting_without_action_authority` → `hatchery_rocm_pressure_report_only_v1` |
| `H-ROCM-STA-GTT-HOST-v1` | `llamacpp-rocm` / `linux-amd-rocm-llamacpp` / `stable` | `STA` / `startup_load` | Grouped startup admission over canonical GTT headroom plus the host-MemAvailable floor | `llm` | `gpu_shared_residency`, `host_memavailable_floor`, `model_type_pool`, `ownership` | `validated` / `unsupported` / `absent` | validated only after grouped startup exists | `hatchery_rocm_sta_v1` | `insufficient_startup_authority` → `hatchery_rocm_startup_block_group_v1` |
| `H-ROCM-REC-GTT-HOST-OWN-v1` | `llamacpp-rocm` / `linux-amd-rocm-llamacpp` / `stable` | `REC` / `prior_epoch_owner_cleanup` | Prior-epoch lifecycle recovery over GTT, host memory, and Lemonade-owned process and claim closure | `llm` | `gpu_shared_residency`, `host_memavailable_floor`, `model_type_pool`, `ownership` | `validated` / `unsupported` / `absent` | validated | `hatchery_rocm_rec_v1` | `unproven_release` → `hatchery_rocm_recovery_block_readiness_v1` |

### Compatibility promotion contracts

| Contract ID | Platform cases | Operation | Scope | Coverage | Relation constraints | Evidence / current state | Runtime / evidence mode | Suite / gate set | Fallbacks |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `H-NPU-FLM-CONFLICT-XDNA2-v1` | `windows-xdna2`: `flm-npu` ↔ `whispercpp-npu`<br>`windows-xdna2`: `flm-npu` ↔ `ryzenai-llm-npu` | `NPC` / `admission` | Cross-family XDNA2 NPU admission compatibility; no capacity or pressure authority | directions: `coexist_by_type_incoming`, `exclusive_incoming`<br>incumbents: `unpinned_idle`, `pinned`, `in_use`<br>model types: `all_declared_by_participant` | `npu_cross_family` | `modeled` / `unsupported` / `absent` | `none` / `synthetic_only` | `npu_compatibility` / `xdna2_npu_flm_conflict_v1` | `insufficient_displacement_authority` → `residency_npu_conflict_preserve_refuse_v1` |

### Promotion roster

| Promotion unit kind | IDs |
| --- | --- |
| Exact cells | `H-ROCM-ADM-GTT-HOST-v1`, `H-ROCM-PRE-GTT-HOST-v1`, `H-ROCM-STA-GTT-HOST-v1`, `H-ROCM-REC-GTT-HOST-OWN-v1` |
| Compatibility contracts | `H-NPU-FLM-CONFLICT-XDNA2-v1` |
<!-- END GENERATED SUPPORT INVENTORY -->

The four operation-specific Hatchery selectors in the inventory raise only their `gfx1151` `llamacpp:rocm` text-only `ADM`, `PRE`, `STA`, or prior-epoch `REC` ceiling to `validated`. Runtime materialization still binds the exact device, artifact and loaded dependency closure, normalized model/configuration/workload, evidence index, and liveness lease. An unrestricted architecture entry such as `openmoss:rocm` is not a wildcard validation grant. The inventory applies the host floor through constraint-profile composition rather than a pseudo-backend row. Multi-device and mixed-provider placement always form separate cells, even when a single-device row validates.

## Platform observations and recovery authority

These are required evidence plans, not claims that the current fork exposes the adapter.

| Platform family | Capacity and pressure observations | Attribution and health | Recovery authority and verified release |
| --- | --- | --- | --- |
| Linux amdgpu ROCm/Vulkan | DRM/amdgpu per-device VRAM or GTT totals/use; signed headroom; `/proc/meminfo` `MemAvailable` for shared/host effects | Durable PCI/DRM identities, topology generation, cgroup/process identity, DRM fdinfo where complete, coherent uncertainty/freshness | Prepared launch journal plus hereditary cgroup/process-tree containment or a cataloged external membership mechanism; release requires tree absence and reconciled GTT/VRAM/host effects. |
| Linux NVIDIA CUDA/Vulkan | NVML or an equally reviewed per-device physical oracle plus host `MemAvailable` effects | PCI/device UUID, process attribution, MIG/topology identity, uncertainty/freshness | Prepared launch journal plus cgroup/process-tree containment; release requires full tree absence and independently observed VRAM/host reconciliation. |
| Windows AMD/NVIDIA ROCm/CUDA/Vulkan | Reviewed DXGI/vendor physical-domain signals with signed margin and host available-memory floor | Stable adapter/device identity, process attribution, topology generation, health and skew | Prepared launch journal atomically bound to a Job Object or an equivalent durable external membership mechanism; verified whole-tree exit plus device/host reconciliation. |
| macOS Metal | Reviewed Metal working-set/domain signal plus independent host available-memory floor | Registry/device identity, process attribution, topology and observation generation | Prepared launch journal plus process-group/endpoint containment that covers descendants; verified process/resource exit plus Metal/host reconciliation. |
| XDNA2 NPU | Device readiness and compatibility/type-slot facts; no capacity credit without a separate complete capacity contract | Durable PCI/accel identity, driver/runtime identity, FLM/RyzenAI model and server identity, freshness and health | Prepared launch journal plus exact process/service membership. A backend acknowledgement is not release; prove the complete process/service and device claim are gone. |

If a provider cannot furnish a complete, fresh, coherent observation for a required constraint, `ADM` and `STA` refuse, an applicable `PRE` cell selects its invalid-evidence disabled behavior, and active recovery claims remain. Compatibility-only NPU rows have no `PRE` authority. If a provider cannot furnish trustworthy ownership and verified release, every effectful action for that scope remains unavailable.

Backend-specific process shape narrows the recovery contract:

- Native single-server families (`llamacpp`, `whispercpp`, `sd-cpp`, `kokoro`, `thenoise`, `openmoss`) still require complete descendant coverage, not a parent PID check.
- Python and async-job families (`vllm`, `thinksound`, `acestep`, `trellis`) require the whole worker/job tree, native terminal-state reconciliation, and cancellation/join evidence.
- FLM may use a system-managed package and self-managed model download. Its recovery cell must distinguish the Lemonade-owned serving process/service from external package/model-store activity and must never infer ownership from name or PID alone.
- Artifact ambiguity transfers the complete plausible artifact, identity, and lifecycle claims to the appropriate quarantine authority. It never counts as released residency.

## Conjunctive promotion suites

The generated inventory projection above is the sole human-readable suite and atomic-gate registry. A promotion unit references the applicable IDs; sharing a fixture does not share its result.

Promotion levels are exact:

- `fallback-only` requires every applicable concrete fallback/refusal, protection, recovery, explanation, compatibility, and liveness suite for that promotion unit.
- `modeled` additionally requires primary architecture evidence, normalized provider/backend contracts, every applicable analytic bound, and deterministic synthetic/property suites. Analytic memory bounds are mandatory only for resource-capacity units.
- `validated` additionally requires exact physical end-to-end evidence for the immutable cell, independent physical oracles, every applicable fault/race row, and its frozen cold-attempt schedule.
- Any valid safety-bound exceedance fails the current predictor/catalog revision. An explanation informs a new revision; it does not waive the failure.

Independent evidence means a measurement or authority path that does not merely repeat the implementation's claim: physical counters and watchdogs, identity-bound process/resource observations, held-out inputs, independent reset attempts, and the anti-rollback witness. It does not require a second human maintainer. Machine-verifiable bundles and clean Ralph review establish artifact conformance; Ivan remains the publication authority.

## Current and first-target state

| Scope | Current accepted capability | Current delivery | Evidence ceiling | Next promotion gate |
| --- | --- | --- | --- | --- |
| All portable target cells at this fork revision | `unsupported` | `absent` | As listed in the support inventory | Implement the complete cell and its fallback first; publish only through a reviewed catalog revision. |
| Hatchery `gfx1151` `llamacpp:rocm` text-only `ADM`, `PRE`, `STA`, and `REC` cells | `unsupported` | `absent` | `validated` | Implement the closed v1 predicate and run every conjunctive Hatchery row under the accepted campaign. Each operation promotes separately. |
| XDNA2 NPU/FLM compatibility promotion contract | `unsupported` | `absent` | `modeled` | Deliver the complete contract and pass its synthetic architecture, conflict, protection, fail-closed relation-response, explanation, and liveness gates. `H-NPU-REC-01` grants no cleanup, claim-replay, participant-recovery, or live runtime authority; later physical work creates separate exact Windows cells. |
| Every other GPU/NPU group | `unsupported` | `absent` | Listed target only | Produce a separate evidence plan and immutable cell; no Hatchery result transfers. |

The exact backup locator, witness provider/identity, and issuer key are mandatory pre-collection deployment bindings. They are not unresolved product policy and cannot be inferred by a harness. Unredacted evidence collection remains unauthorized; if a required proof cannot be captured safely, collection stops for a separate private-storage and access decision.

Issue 35 accepts this design and promotion contract. Implementation, catalog generation, harness construction, deployment binding, and physical qualification proceed under the next implementation route.
