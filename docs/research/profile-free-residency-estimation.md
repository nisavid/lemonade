# Profile-free residency estimation for Hatchery llama.cpp/ROCm

Status: accepted Issue [#35](https://github.com/nisavid/lemonade/issues/35) v1 Hatchery predictor design input; not a current implementation or physical validation result.

## Finding

A conservative, profile-free estimate is feasible for the initial, tightly
versioned Hatchery configuration profile. Lemonade does not need to wait for a portable
architecture-by-architecture formula, and it does not need to require a profile
for every model.

The best available formula is executable source, not a generic
`parameters * dtype` approximation: use the pinned llama.cpp build's
`no_alloc` model-and-context construction. llama.cpp already uses this path to
estimate model, context, and compute buffers without allocating them. It reads
the real GGUF metadata, constructs the selected backend layout, reserves the
configured graphs, and asks each backend buffer type for its allocation size.
See [`common_get_device_memory_data()` in the pinned llama.cpp
source](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/common/fit.cpp#L29-L175).

This result is an exact allocation *plan for that source revision and
configuration*, not by itself a complete physical-residency bound. A useful
bound adds the few allocations omitted from the breakdown, known load and
concurrency transients, and an empirically validated residual for lazy
backend/runtime/driver allocations and measurement uncertainty.

The initial formula is:

```text
planned = llama.cpp no_alloc(model + context + compute)
        + separately derived output and known load transients

bound(domain) = project(planned + allowed lifecycle overlap, domain)
              + validated_residual(domain)
```

On Hatchery, `domain` is a vector, not a scalar:

```text
(GTT mapping consumption, system-memory availability consumption)
```

GTT-backed allocations use physical system RAM on Strix Halo. They must count
against both the GTT mapping budget and the host-memory floor; they must not be
treated as independent VRAM and RAM bytes and added twice to one total. AMD's
Strix Halo guidance describes GPUVM as per-process virtual address space,
GART/GTT as mapping limits rather than a separate physical-memory pool, and GTT
as dynamic system-RAM mappings used as the primary AI-compute pool. See [AMD's
Strix Halo system-optimization guide](https://rocm.docs.amd.com/en/docs-7.2.0/how-to/system-optimization/strixhalo.html).

## V1 applicability profile

The result applies only when the estimator mirrors the load exactly. The
minimum identity includes:

- model artifact hash, including any projector or draft model;
- llama.cpp binary/source identity and build options;
- ROCm/HIP runtime, AMDGPU driver/kernel, and firmware identities;
- hardware and UMA/GTT configuration;
- model placement and offload parameters;
- context size, batch and micro-batch sizes, maximum sequences/parallelism,
  output count, KV-cache types, flash attention, embeddings/reranking mode,
  and other graph-affecting options;
- mmap, mlock, tensor checking, unified-memory, and pinned-host-buffer options;
- Lemonade subprocess and load-overlap policy.

### Current checkout versus refresh target

The research base at fork commit `13a5c6aa` pins `rocm-stable` to `b9586`; that is
the source context in which this research began, not the backend selected by
the accepted v1 design. Immutable upstream Lemonade commit `3e532596` pins
`rocm-stable` to release label `b10397` and TheRock 7.13.0; see the upstream
[`backend_versions.json`](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/resources/backend_versions.json#L48-L52)
and [ROCm stable asset selection](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/backends/llamacpp/llamacpp_server.cpp#L176-L194).
The accepted v1 design in this note targets that refreshed upstream launch shape,
not `b9586` behavior.

The `b10397` label is not a sufficient source identity. Lemonade's build-only
llama.cpp fork checks out the live ggml-org repository independently in each
job, while the release job later renames collected artifacts using its own
fresh checkout's revision count; see the [build checkout](https://github.com/lemonade-sdk/llama.cpp/blob/53efdf1b0e04f27fbbe7f177fffd92487b59a1ca/.github/workflows/release.yml#L29-L48),
[tag calculation](https://github.com/lemonade-sdk/llama.cpp/blob/53efdf1b0e04f27fbbe7f177fffd92487b59a1ca/.github/actions/get-tag-name/action.yml#L10-L17),
and [release rename](https://github.com/lemonade-sdk/llama.cpp/blob/53efdf1b0e04f27fbbe7f177fffd92487b59a1ca/.github/workflows/release.yml#L1023-L1137).
For the pinned Ubuntu ROCm 7.13 asset, the official build job checked out
ggml-org commit `680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963` and packaged it internally
as `llama-b10394`; the release job subsequently renamed the collected archive
to a `b10397` filename. See [the Ubuntu ROCm 7.13 build job](https://github.com/lemonade-sdk/llama.cpp/actions/runs/31600709220/job/94127102963)
and [the release job](https://github.com/lemonade-sdk/llama.cpp/actions/runs/31600709220/job/94179956648).

Consequently, the v1 configuration profile must record the downloaded asset's SHA-256 and the
binary's reported version/source commit. The current upstream label is a lookup
key, not evidence that every asset was compiled from the revision whose count
is 10397. All llama.cpp source claims below cite the resolved Linux ROCm 7.13
artifact source, `680a9ae6`.

Upstream now passes a local GGUF or `-hf`, the selected context size, optional
projector and speculative inputs, arbitrary recipe arguments, and
`--load-mode none` on an iGPU; see [upstream argument construction](https://github.com/lemonade-sdk/lemonade/blob/3e53259600bf9076d51fd9dee889732e0df0c11a/src/cpp/server/backends/llamacpp/llamacpp_server.cpp#L289-L447).
Those choices expose memory-affecting cases that the v1 configuration profile must either
model explicitly or exclude.

### V1 configuration predicate

A load is in the v1 text-only configuration profile only when every item below is true:

- the host is Hatchery's Linux `gfx1151` Strix Halo configuration and the
  backend is the upstream `rocm-stable` 7.13 asset identified by its verified
  artifact hash and reported source commit;
- the recipe resolves exactly one local text-model GGUF and no auxiliary model,
  projector, adapter, control vector, or remote/model-repository load;
- the server is launched with explicit `--load-mode none`, `--fit off`,
  `--gpu-layers all`, a finite `--ctx-size`, `--parallel 1`,
  `--batch-size 2048`, and `--ubatch-size 512`;
- the estimator receives those same effective values plus the resolved KV
  types, flash-attention decision, output limit, device placement, and every
  other model/context parameter consumed by the dry construction;
- `--cache-ram 0`, `--no-cache-idle-slots`, `--no-cache-prompt`, and
  `--ctx-checkpoints 0` are explicit, and no slot-save path or slot
  save/restore operation is permitted during the validated lifetime;
- `llamacpp_args` is empty. The fixed transport, metrics, Jinja, reasoning, and
  UI arguments Lemonade owns are part of the fingerprint; no recipe-supplied
  argument may change memory, graph shape, allocation, placement, caching, or
  concurrency;
- Lemonade serializes candidate load transitions and admits at most the one
  candidate-over-residents lifecycle overlap included in the bound. A resident
  accepts one active inference sequence; any broader per-process or admission
  concurrency is outside the profile.

Explicit `--fit off` and `--gpu-layers all` prevent llama-server's own
free-memory-dependent fitting from changing placement behind Lemonade's
capacity decision. The resolved source otherwise defaults to automatic layer
fitting, a 1 GiB device target margin, and automatic load mode; see [the
resolved common defaults](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/common/common.h#L440-L476).
The fixed batch values are the resolved source defaults, made explicit so a
future default change becomes an identity mismatch rather than silent drift.

Single-slot operation deliberately closes the v1 concurrency surface. The
resolved server converts automatic parallelism to four slots with unified KV;
see [server startup](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/tools/server/server.cpp#L140-L156).
V1 does not rely on that mutable default.

### Explicitly outside V1

- **Multimodal and projector loads:** `mmproj`, `mmproj-auto`, multimodal input,
  and related media options are excluded. llama-server has a separate
  `mtmd_get_memory_usage()` estimate, so this can become an independently
  validated component later; see [multimodal fitting](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/tools/server/server-context.cpp#L1050-L1134).
- **Draft, MTP, DFlash, and speculative decoding:** all auxiliary draft models
  and speculative contexts are excluded. The server already dry-constructs a
  separate draft/MTP component before fitting the target, which is a plausible
  later composition point; see [speculative fitting](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/tools/server/server-context.cpp#L1136-L1189).
- **`hf_load`:** excluded because it lets llama-server resolve and download the
  model and optional projector rather than supplying the exact local artifact
  set to the estimator. It can enter a later configuration profile only after resolution produces
  an immutable local manifest and all components are estimated.
- **Recipe-supplied memory-affecting arguments:** excluded rather than guessed.
  A later allowlist may admit an option only when its normalized effective
  parameter is passed into the planner or separately bounded.
- **Prompt RAM cache, idle-slot snapshots, context checkpoints, and on-disk slot
  save/restore:** disabled in V1. The resolved server otherwise defaults to an
  8192 MiB prompt-cache ceiling and 32 context checkpoints per slot; see [server
  cache defaults](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/common/common.h#L603-L615),
  [prompt-cache allocation](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/tools/server/server-context.cpp#L1393-L1419),
  and [checkpoint snapshots](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/tools/server/server-context.cpp#L2337-L2384).
- **Unmodeled concurrency:** parallelism greater than one, simultaneous
  candidates, and any request/load overlap not represented in both dry planner
  parameters and Lemonade's lifecycle reservation are excluded.

An excluded configuration reports an estimator identity mismatch and follows
the `unknown` fallback below. It does not borrow capacity credit from the
  text-only profile.

## Exact and derivable terms

### Model tensors

The model term comes from the actual GGUF tensor directory and the
backend allocation calculator, not from nominal parameter count or file size.
GGUF records each tensor's name, dimensions, type, and aligned data offset; see
the official [GGUF format specification](https://github.com/ggml-org/ggml/blob/master/docs/gguf.md)
and [llama.cpp GGUF declarations](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/ggml/include/gguf.h).

In `no_alloc` mode, llama.cpp calculates the planned model allocation by using
the selected buffer type's size calculator for the constructed tensor contexts;
see [`llama_model::memory_breakdown()`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/src/llama-model.cpp#L1744-L1761).
The generic allocator accounts for the buffer type's per-tensor allocation
size, alignment, and maximum-buffer splits; see
[`ggml_backend_alloc_ctx_tensors_from_buft_impl()`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/ggml/src/ggml-alloc.c#L1167-L1244).
The pinned HIP implementation additionally applies quantized-row padding and a
128-byte buffer alignment; see the HIP/CUDA buffer type's
[`get_alloc_size`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/ggml/src/ggml-cuda/ggml-cuda.cu#L881-L930).

This makes the source-derived model allocation substantially more faithful than
`parameters * average bits`: mixed tensor formats, tied or absent tensors,
per-layer variation, quantization block structure, padding, alignment, and
offload placement are already represented.

### KV and other persistent context memory

The KV term is also source-derived. For each layer, llama.cpp selects the
offload buffer type and constructs K and V tensors from the layer-specific GQA
key/value widths, configured cache types, configured KV capacity, and stream
count; architectures such as MLA can omit the separate V tensor. See [KV-cache
construction](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/src/llama-kv-cache.cpp#L206-L303).
The same backend size calculator determines the allocation in `no_alloc` mode;
see the [KV memory-breakdown implementation](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/src/llama-kv-cache.cpp#L681-L696).

For ordinary llama.cpp operation, KV capacity is allocated when the context is
created. A nearly empty versus nearly full logical cache does not imply a
corresponding change in this core allocation. Running both workloads is still
valuable because execution can trigger lazy backend allocations and transient
peaks; the campaign does not use it to fit a KV formula that the source already knows.

### Compute and attention buffers

Activations are not well modeled as one persistent tensor per token or layer.
llama.cpp constructs and reserves scheduler graphs for prompt processing and
token generation, then reports the scheduler's planned buffer sizes. The
source explicitly labels this as reserving a worst-case graph and repeats the
prompt graph after token-generation reservation to avoid inference-time
allocator growth. See [context graph reservation](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/src/llama-context.cpp#L581-L703).

This term follows architecture-specific operations, selected kernels, flash
attention, batch/micro-batch, output and sequence counts, and placement. It is
therefore a better initial estimator than a portable closed-form activation
formula. It is not an unconditional proof: the same source contains a TODO
questioning whether the reserved prompt graph is the worst case for
multi-stream KV caches. That source-admitted seam must be exercised in
validation rather than covered by assertion.

### Output buffer

The public `llama_memory_breakdown` aggregates model, memory-context, and
scheduler-compute buffers; see
[`llama_context::memory_breakdown()`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/src/llama-context.cpp#L3260-L3284).
It does not include `buf_output`, so Lemonade must add that term.

The pinned source supplies an exact formula for its configured maximum: float
storage for logits, embeddings, optional next-token embeddings, and backend
sampler logits/probabilities, plus token storage for samples/candidates. It
allocates the result in a CPU buffer or the output device's pinned host buffer.
See [`llama_context::output_reserve()`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/src/llama-context.cpp#L2035-L2125).
This is derivable from vocabulary size, output embedding size, batch size,
maximum outputs, enabled outputs, and sampler configuration.

### Load-time staging

With mmap disabled, the model loader can use four pinned-host staging buffers
for asynchronous device upload when tensor checking is also disabled and the
backend exposes the required capabilities. The pinned source selects 1 MiB per
buffer for ordinary reads or roughly 64 MiB plus alignment for aligned/direct
I/O. See [`llama_model_loader::load_all_data()`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/src/llama-model-loader.cpp#L1415-L1510).

This is a conditional, bounded transient. The estimator must evaluate the same
flags and capabilities as the actual launch; it must not blindly add or omit
the buffers. Model-file page cache and mapped-file behavior must be treated
separately if mmap is enabled in another configuration profile.

### Host/device duplication and mappings

Do not infer a model-sized host duplicate merely because Hatchery uses unified
physical memory, and do not infer that every tensor is device-placed merely
because Lemonade requests full offload. The dry construction reports actual
CPU/host versus HIP buffer-type placement. CPU-resident tensors count as host
allocations; HIP device buffers consume GTT-backed system RAM in the initial
configuration profile.

With `--load-mode none`, the model file is not retained as the tensor backing
mapping. The source path above streams data through bounded upload buffers when
its conditions hold, rather than keeping a second model-sized GGML host
allocation.
Ordinary buffered file reads can still warm the filesystem page cache. That is
not an owned model-tensor buffer, but it can affect the load-time host-memory
trace and differs when direct I/O is enabled, so validation must cover the
actual I/O mode.

Pinned output and upload buffers are separate host allocations and must be
projected according to how the runtime maps them. A GTT-backed allocation is
one physical system-RAM effect subject to two constraints: count it once in the
physical-memory effect, while checking both GTT mapping headroom and the host
memory floor.

### Concurrent and lifecycle transients

The single-process formula does not know whether Lemonade retains an old model
while a candidate process starts, permits two loads concurrently, or waits for
post-unload release. Admission needs a reservation for the complete permitted
lifecycle overlap:

```text
overlap = sum(each resident bound)
        + candidate startup/load bound
        + any simultaneously admitted candidate or retiring process
```

Serializing candidate loads can make this term small and deterministic. If
Lemonade permits broader concurrency, the bound must represent it explicitly.
Request concurrency within one llama-server process must be represented by the
same maximum sequence/parallelism and batching settings supplied to the dry
construction; request parsing, token vectors, HTTP buffers, and similar host
heaps belong in the residual until separately derived.

## Terms that still require an empirical residual

The following are not safely recoverable from model metadata alone:

- lazy HIP streams, events, hipBLAS handles, command and graph state;
- backend kernel workspaces and scratch-pool high-water allocations reached
  only when particular operations execute;
- HIP runtime and AMDGPU driver allocations, GPU page tables, and process
  runtime state;
- allocator fragmentation and rounding outside the planned GGML buffers;
- server, HTTP, tokenizer, request, and response heaps not covered above;
- measurement granularity, attribution gaps, sampling interval, and release
  latency.

These are demonstrably real rather than hypothetical. The resolved backend
creates streams and hipBLAS handles lazily and owns per-stream pools; see
[`ggml_backend_cuda_context`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/ggml/src/ggml-cuda/common.cuh#L1415-L1526).
Its legacy scratch pool rounds a new allocation to 105 percent, aligns it to
256 bytes, and caches released buffers until reuse or backend destruction; see
[`ggml_cuda_pool_leg`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/ggml/src/ggml-cuda/ggml-cuda.cu#L419-L530).
llama.cpp's own memory table labels the device-memory remainder after its
model, context, and compute accounting as `unaccounted`; see
[`common_memory_breakdown_print()`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/common/fit.cpp#L817-L903).

Persistent model buffers use `hipMalloc` by default through the backend's HIP
aliases, while an opt-in environment setting uses `hipMallocManaged`; see
[`ggml_cuda_device_malloc()`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/ggml/src/ggml-cuda/ggml-cuda.cu#L138-L165).
Pinned host buffers use `hipHostMalloc` through the same aliases and can fall
back to an ordinary CPU buffer; see the [host-buffer allocator](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/ggml/src/ggml-cuda/ggml-cuda.cu#L1269-L1320).
The allocation class therefore changes the physical-domain effect and belongs
in the applicability identity.

An arbitrary large “fudge factor” is not a confidently safe bound until it has
evidence behind it. It can be selected conservatively for initial experiments,
but validation must show that it bounds the positive residual for the declared
configuration profile. The residual need not be precise: it may deliberately be loose, provided
it remains useful and never understates the observed safety peak.

## Measurement on Strix Halo

Use two complementary measurements:

1. Global AMDGPU GTT `total` and `used` report the system-wide mapping budget.
   The kernel documents these sysfs counters as GTT memory-usage information;
   see [AMDGPU driver memory usage](https://docs.kernel.org/gpu/amdgpu/driver-misc.html#gpu-memory-usage-information).
2. DRM `fdinfo` can attribute resident and total memory by region to a client,
   when the driver exposes the relevant keys; see the kernel's [DRM usage
   statistics specification](https://docs.kernel.org/next/gpu/drm-usage-stats.html).

Also sample host `MemAvailable` and relevant process RSS/PSS. Global GTT delta
is the capacity effect; per-client `fdinfo` is attribution evidence, not a
replacement for the system-wide counter. Baseline-subtract unrelated activity
where possible and retain uncertainty for concurrent applications.

Do not reuse llama.cpp's stock `--fit` decision as Lemonade's capacity policy.
Its interface says that fitting to device memory assumes unlimited system
memory; see [`common/fit.h`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/common/fit.h#L14-L27).
In the resolved Linux HIP backend, device memory starts with `hipMemGetInfo`, but
an integrated device replaces the reported free value with a `/proc/meminfo`
availability calculation while retaining the HIP total; see
[`ggml_backend_cuda_device_get_memory()`](https://github.com/ggml-org/llama.cpp/blob/680a9ae63d60d35c21a0dcd7d3fabdb9c6bfc963/ggml/src/ggml-cuda/ggml-cuda.cu#L4760-L4799).
That mixed value does not independently enforce Hatchery's GTT mapping limit
and host-memory floor.

## Validation and promotion threshold

Profiles of individual models are optional refinements. The prerequisite
for automatic capacity action is validation of the *profile-wide analytic method
and residual envelope*, not a stored profile for each admitted model.

Calibrate on a deliberately varied set that covers the profile's supported
surface, including:

- dense and mixture-of-experts models;
- ordinary multi-head, grouped-query/multi-query, and any supported MLA,
  recurrent, sliding-window, or hybrid memory structures;
- materially different GGUF quantization families and model sizes;
- low and high declared context sizes and output counts, with the fixed v1
  batch, micro-batch, and single-sequence settings;
- flash-attention and other supported graph-affecting modes;
- text completion and chat configurations only. Embedding, reranking,
  multimodal, projector, draft, MTP, speculative, adapter, and auxiliary-model
  paths are separate applicability cells.

Hold out different models, quantizations, and configurations from calibration.
For every calibration and held-out case, measure the complete lifecycle:

1. clean baseline and cold subprocess/runtime initialization;
2. candidate start and model load;
3. loaded idle and a nearly empty logical cache;
4. maximum prompt-processing shape;
5. near-maximum context and single-sequence generation;
6. unload/exit and verified release;
7. the largest concurrency/old-new overlap the router permits;
8. representative external memory pressure.

The harness must capture the effective llama-server argv and startup-resolved
parameters. It must also prove that prompt RAM cache and context checkpoints are
disabled, that a second request does not create an unmodeled active sequence,
and that every excluded recipe shape or argument produces `unknown` rather than
v1 capacity credit.

For each physical domain, compute:

```text
positive_residual = observed_peak_effect - source_planned_effect
validated_residual >= max(all positive_residuals)
                    + measurement_uncertainty
                    + reviewed_safety_margin
```

Promotion requires all valid held-out runs to remain within the proposed bound. Every
valid exceedance blocks the current predictor-rule revision; explaining the miss
only makes it calibration input for a new revision with a newly frozen, disjoint
held-out campaign. A predeclared invalid-attempt rule may exclude bad evidence,
but the result cannot be reclassified after observation. Averages and high percentiles
are utility measures, not safety boundaries. Repeat trials must cover
nondeterminism and sampling uncertainty. Revalidate after a material change to
the llama.cpp binary/build, ROCm/HIP, driver/kernel, hardware or GTT setup, or a
graph/allocation-affecting recipe option. A new unmatched identity does not
inherit credit from the old residual.

After promotion, any model/configuration inside the validated applicability
envelope can use the analytic bound without per-model profiling. A compatible
stored profile may tighten a bound only under an explicit policy and exact
fingerprint; failure to find one must leave the conservative analytic bound
available, not disable estimation.

## Admission, reclamation, and fallback

Validation of this estimator can authorize a capacity conclusion: whether the
candidate fits and, if not, how much eligible residency must be released. It
does not by itself authorize a victim or a destructive action. Automatic
reclamation separately requires validated resident ownership and identity,
pin/in-use eligibility, race-free reservation, unload/termination behavior,
and post-action release verification.

Accepted fallback states are explicit:

| Estimator state | Capacity behavior |
| --- | --- |
| Exact configuration-profile match; analytic method and residual validated | May supply capacity credit and automatic admission decisions in an independently authorized runtime cell. |
| Exact configuration-profile match; method not yet validated | Report/shadow only; no capacity credit or automatic hard reclamation. |
| Identity mismatch, unsupported option, dry construction failure, or unknown placement | Unknown; refuse predictive capacity automation rather than substitute file size or another profile's residual. |
| Compatible optional per-model profile | May tighten only according to a separately accepted profile policy; otherwise retain the validated analytic bound. |

“Fallback” names a behavior, not merely “safe”: unknown
estimation, no speculative capacity credit, no capacity-driven eviction, and a
clear diagnostic. Explicit user-requested load/unload behavior remains governed
by its own policy.

## Follow-up scope

A portable, closed-form estimator remains useful for backends that cannot
perform a faithful dry construction, for explanation and independent
cross-checking, and for early fallback before a backend cell exists. Collected
profiles can help identify missing terms and fit conservative residuals.

That work does not block the initial llama.cpp/ROCm implementation. It does not
estimate a “supremum” merely by fitting the observed maximum: any proposed
portable envelope needs declared applicability, held-out coverage, measurement
uncertainty, a safety margin, and explicit behavior for architectures or
options outside its support.

Separate component tickets can broaden the resolved backend without weakening
the v1 predicate: projector memory through `mtmd_get_memory_usage`; draft/MTP
through a second dry construction; explicitly capped prompt RAM and checkpoint
snapshots; immutable manifests for `hf_load`; a reviewed argument classifier;
and parallel/concurrent execution whose complete maximum shape is supplied to
the planner and exercised physically. Each component needs its own identity,
residual, and composition rule before it contributes capacity credit.

## Accepted policy answers

1. `modeled` means source-derived with complete normalized contracts and synthetic evidence but without the required physical end-to-end validation. A reviewed catalog may assign `validated_predictor` to every model/configuration inside the exact proven predicate without requiring a stored per-model profile.
2. Dry-construction failure, identity mismatch, unsupported configuration, or unknown placement produces `unknown` and refuses predictive capacity automation before reclamation or spawn. File size and another profile's residual are not substitutes.
3. Estimator and reclamation-mechanism promotion remain separate. A sound byte estimate cannot prove resident eligibility, ownership, action safety, or verified release.
4. A residual never carries automatically across a backend, runtime, driver, or other material fingerprint change. The new predicate may reuse the old envelope only in shadow/report mode while a new revision requalifies.
5. Safety permits no valid bound exceedance. The accepted Hatchery campaign separately requires per-projection overhead and two-resident feasibility before granting capacity credit, so a technically safe but impractical predictor remains report-only until a new reviewed rule improves its utility.
