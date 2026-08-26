# Linux AMD GTT interval and differential evidence

Research snapshot: 2026-08-26 UTC
Decision recheck: 2026-08-26 UTC

Repository baselines:

- fork `origin/main`: `f0e172f5500c32128dd0eb64b512b5efb0bf97df`;
- Lemonade `upstream/main`: `61c4e491439737f09d54e01ba9c155102cce507b`;
- Linux stable tag `v7.1.8`: `25c76bea853d0db65b51fb4697a47cbfd9e35e76`;
- Linux mainline research snapshot: `45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229`.

The decision recheck compared current upstream `main` at
`92fe3aa88998fb94f8614da4859bac8fc3476ef0`; none of the relevant Lemonade
metric or system-information paths changed after the research baseline.

## Answer

No evaluated stable Linux, DRM, ROCm, or Lemonade interface can supply a
mutation-complete profiling observation interval for Hatchery's AMD GTT domain.
The current interfaces divide into authoritative point observations, incomplete
event streams, and enforcement mechanisms that do not cover AMD GTT. This
prevents treating a poller as the existing dense interval source; it does not
make point observations diagnostic-only under every evidence boundary.

The selected first-release boundary permits repeated controlled differential
points to authorize an exact-fingerprint retained-GTT calibration. It does not
let that retained result stand in for an unobserved load or request transient.
Full admission authority still requires a separately complete conservative
lifecycle envelope.

The accepted source contract requires one atomic registration/checkpoint
boundary, a source epoch, a dense watermark that advances exactly once for every
retained relevant resource-state change, detectable history loss, and an atomic
final drain. It also requires every frame to carry both the device-wide value and
the complete target-owner projection. These are source requirements, not
properties the Server can synthesize around a sampler. See the reviewed
[`ProfilingIntervalObservationSource`](https://github.com/nisavid/lemonade/blob/f0e172f5500c32128dd0eb64b512b5efb0bf97df/src/cpp/include/lemon/residency/profiling_provider.h#L68-L157).

Periodic point reads are insufficient at every cadence. An interval in which a
GTT allocation appears and is released between two reads is observationally
identical to an interval with no change. Incrementing a watermark per read would
record acquisitions, not resource mutations; incrementing only when values differ
would erase the intervening allocation. Neither convention can detect the lost
history.

## Selected first-release boundary

The activation-capable point boundary is a Server-owned differential
retained-footprint calibration, not a sampled interval. It binds the exact
deployment, boot, device, kernel and driver, backend and dependency closure,
model artifact, normalized configuration, workload, containment, and catalog
identities. The Server exclusively owns the intervention and queues competing
Lemonade model work.

Before each boot epoch, the existing
[campaign noise procedure](hatchery-campaign-parameters.md#noise-and-baseline-procedure)
uses no-target controls to freeze the maximum quiescent GTT plateau range plus
read and timing uncertainty as `N_gtt`; attribution uncertainty remains the
disjoint `X_gtt` term. The campaign revision freezes `N_gtt`, `X_gtt`, one
nonnegative safety margin `M_gtt`, positive calibration and validation
repetition counts, and their disjoint partition before any model observation.
It also freezes the plateau selection rule: 50 ms nominal cadence, at most
100 ms between read initiations, an exact five-second contiguous window, and at
least 50 valid points per plateau. Each window begins with the first scheduled
acquisition on or after the Server-owned phase-ready marker and includes every
scheduled acquisition whose start falls before the five-second end boundary.
The baseline, load, and release markers are respectively emitted after the
exclusive gate becomes quiescent, after the model load reports completion, and
after release reports completion. A window is stable only when every read and
identity check succeeds, its maximum minus minimum GTT use is at most `N_gtt`,
and no actor identity changes. A gap, invalid point, wider range, or actor
change rejects the repetition; collection never resets or selects a later
favorable subwindow.

Every repetition performs stable baseline -> loaded plateau -> verified
release, with multiple authoritative `mem_info_gtt_used` points at each plateau.
For calibration repetition `r`, checked signed arithmetic derives
`D_r = max(loaded_r) - min(baseline_r)`. Every `D_r` must be nonnegative; let
`D_gtt` be their maximum in the frozen calibration partition. The candidate
retained bound is `B_retained_gtt = D_gtt + N_gtt + X_gtt + M_gtt`, with checked
addition. Every disjoint validation repetition derives the same checked delta,
which must remain at or below that bound. For every repetition, form the release
envelope from that repetition's pre-load plateau as
`[max(0, min(baseline_r) - N_gtt), max(baseline_r) + N_gtt]`, rejecting upper
overflow; every post-release point must lie inside it. A validation exceedance
rejects the candidate and cannot enlarge the same revision. Identity drift,
background movement beyond the allowance, missing release, or arithmetic
failure also rejects the whole transaction.

The bound is never an average or percentile. DRM fdinfo is owner-corroboration
evidence when it is complete; missing process attribution is recorded rather
than converted to zero and does not change the global counter's point accuracy.
The causal attribution comes from the exclusive Server-owned A-B-A intervention
and its release closure.

This result covers stable retained GTT occupancy only. The catalog must supply a
separate conservative bound for startup, load, prompt processing, generation,
allocator, and overlap transients before the result can participate in a
complete admission manifest. Point sampling may corroborate or falsify that
separately justified envelope and its residual, but it cannot prove their
completeness or claim mutation-complete history.

## Candidate assessment

| Candidate | Strongest supported claim | Missing contract properties | Fail-closed disposition |
| --- | --- | --- | --- |
| Fork AMD GTT point source | One global point bracketed by two contained-owner scans for a stable process/descriptor set, with shared GTT rejected | Global and owner counters are not one kernel-atomic snapshot; no resource epoch, mutation cursor, retained history, atomic interval begin, or drain | Retain as a point source; the Server may compose its points into differential calibration but never adapt it into an interval cursor |
| Upstream Lemonade, `mem_info_gtt_used`, libdrm, ROCm SMI, or AMD SMI point queries | Authoritative current per-device or per-process usage | No mutation-complete history; process totals do not close the device domain | Global GTT points may enter the differential retained-footprint boundary; otherwise diagnostics and checkpoints only |
| DRM fdinfo | Current resident usage per DRM client, with explicit shared usage | No subscription, ordering, epoch, or loss signal; shared objects make client sums non-additive | Corroborate owner attribution when complete; reject ambiguity rather than infer zero |
| ROCprofiler HIP/HSA allocation and KFD event tracing | Target-process API allocations plus selected SVM migration, mapping, fault, queue, and dropped-event records | Does not cover every TTM GTT resource-manager mutation or every external DRM client; API allocation is not committed physical placement | Correlation evidence only |
| AMDGPU tracepoints through tracefs/perf | Selected BO creation, requested move, VM, and submission activity; trace-buffer transport loss can be detected | Existing events do not describe each committed GTT counter change, complete device identity, and stable owner projection; `amdgpu_bo_move` fires before the move | Exact-kernel diagnostic trace only |
| Generic `gpu_mem_total` tracepoint | Its schema represents GPU-memory allocation, free, import, and unimport by GPU id, PID, and size | AMDGPU does not emit it; PID still lacks a stable owner generation and atomic subscription baseline | Unavailable for AMD GTT |
| eBPF on current tracepoints, kprobes, or fentry/fexit | A custom exact-kernel prototype can order emitted records and report ring reservation failure | Tracepoints and probe sites are unstable; no stable hook exposes an atomic GTT checkpoint plus every committed counter mutation and owner transition | Do not ship as activation evidence without a separately reviewed kernel-fingerprint-specific prototype and semantic coverage proof |
| cgroup v2 `dmem` | Per-cgroup current/limit accounting, and a retained peak in newer mainline, for driver-registered regions | Hatchery kernel 7.1.8 and current mainline amdgpu register VRAM, not GTT; no current GTT charge, peak, or limit exists | Strongest future enforcement route, currently unavailable for this cell |
| Device cgroup BPF, device namespaces, KFD device checks, DRM master, or DRM leases | Can restrict later device-node access or lease display resources | Does not revoke existing descriptors, bindings, mappings, BOs, or submitted work; DRM master and leases do not cover render-node GTT | Admission hardening only |
| Fresh process cgroup plus freeze or kill | Stable target task scope and a userspace completion boundary | Freezing tasks is not a GPU queue fence; resident BOs and foreign or kernel clients remain outside the scope | Orchestration only; not interval closure |
| AMD process isolation | Serializes selected engine use and clears selected shader state between processes | Does not partition GTT or exclude other processes from later execution and memory changes | Security and scheduling only |
| AMDGPU debugfs eviction, reset, or point dumps | Device-wide recovery operations and current diagnostic state | Debugfs has no stable ABI; operations are destructive and provide neither retained history nor complete attribution | Diagnostics and recovery only |
| Dedicated host, full-function assignment, or whole-device VM | Reduces ordinary host userspace competition | Changes the topology and does not itself measure target peak, exclude driver or guest-kernel demand, or retain GTT history | A separately qualified environment, not evidence for this cell |

## Current point sources

The Linux amdgpu sysfs attribute returns the current value of
`ttm_resource_manager_usage()` for the device's GTT manager. It is a locked
snapshot of a counter, not a change journal. The kernel documents it as current
used GTT and implements one value per read in
[`amdgpu_gtt_mgr.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/amd/amdgpu/amdgpu_gtt_mgr.c#L57-L74).
The ioctl `AMDGPU_INFO_GTT_USAGE` reads the same manager counter. Resource
initialization, finalization, and the getter serialize on the TTM LRU lock, so
each value is an authoritative current device point, not an atomic interval or
owner projection. See
[`amdgpu_kms.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/amd/amdgpu/amdgpu_kms.c#L803-L812)
and
[`ttm_resource.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/ttm/ttm_resource.c#L617-L635).

AMD SMI maps its GTT usage query to `mem_info_gtt_used`; a tool using
`AMDGPU_INFO_GTT_USAGE` reaches the same TTM manager value through the ioctl
above. Agreement among those tools therefore confirms path and value
consistency rather than three independent sensors. The current AMD SMI CLI
[selects the APU memory type](https://github.com/ROCm/rocm-systems/blob/920418c4848ee640db2ee9997ddefe81a5043e32/projects/amdsmi/amdsmi_cli/amdsmi_helpers.py#L1295-L1355)
and calls the
[public memory-usage API](https://github.com/ROCm/rocm-systems/blob/920418c4848ee640db2ee9997ddefe81a5043e32/projects/amdsmi/amdsmi_cli/subcommands/default.py#L195-L215);
the lower-level implementation maps
[`kDevMemUsedGTT` to `mem_info_gtt_used`](https://github.com/ROCm/rocm-systems/blob/920418c4848ee640db2ee9997ddefe81a5043e32/projects/amdsmi/rocm_smi/src/rocm_smi_device.cc#L102-L107),
then maps `RSMI_MEM_TYPE_GTT` to that attribute and
[reads it](https://github.com/ROCm/rocm-systems/blob/920418c4848ee640db2ee9997ddefe81a5043e32/projects/amdsmi/rocm_smi/src/rocm_smi.cc#L4270-L4340).

The fork's Linux status metric reads the same GTT attribute, but on an APU it
sums GTT and VRAM used bytes before the renderer labels the value `VRAM`. It is
a useful global GPU-memory corroboration, while a direct
`mem_info_gtt_used` read is the precise GTT-only input. See
[`metrics_linux.cpp`](https://github.com/nisavid/lemonade/blob/f0e172f5500c32128dd0eb64b512b5efb0bf97df/src/cpp/server/platform/metrics_linux.cpp#L19-L39)
and its
[`GTT read`](https://github.com/nisavid/lemonade/blob/f0e172f5500c32128dd0eb64b512b5efb0bf97df/src/cpp/server/platform/metrics_linux.cpp#L146-L188).

DRM fdinfo defines `drm-resident-<region>` as a client's currently resident
buffers. Its deprecated amdgpu `drm-memory-<region>` key is an alias. A separate
`drm-shared-<region>` value identifies buffers shared with another file. Those
semantics support a point owner projection but not an interval or an additive
device total; see the kernel's
[`DRM client usage stats`](https://docs.kernel.org/next/gpu/drm-usage-stats.html#memory-stats).
AMDGPU obtains those per-VM values under a VM statistics lock that is independent
of the TTM global-counter lock, so the two interfaces cannot form one
kernel-atomic global-plus-owner snapshot. See
[`amdgpu_vm.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/amd/amdgpu/amdgpu_vm.c#L1263-L1269).

The fork adapter makes the strongest safe composition of those points. It takes
two complete contained-process fdinfo passes around one global sysfs read,
requires the process members and descriptor/client maps to remain equal, rejects
nonzero shared GTT, and returns the closing containment-snapshot generation for
both samples. That generation proves containment progress, not GTT mutation
progress. See
[`linux_amd_gtt_point_observation_source.cpp`](https://github.com/nisavid/lemonade/blob/f0e172f5500c32128dd0eb64b512b5efb0bf97df/src/cpp/server/residency/platform/linux_amd_gtt_point_observation_source.cpp#L827-L899).

Current upstream Lemonade adds no stronger source. At the researched revision it
reads `mem_info_gtt_used` and `mem_info_gtt_total` synchronously for AMD APU
pressure in
[`system_info.cpp`](https://github.com/lemonade-sdk/lemonade/blob/61c4e491439737f09d54e01ba9c155102cce507b/src/cpp/server/system_info.cpp#L4176-L4241),
and its Linux metrics path performs another direct sysfs read in
[`metrics_linux.cpp`](https://github.com/lemonade-sdk/lemonade/blob/61c4e491439737f09d54e01ba9c155102cce507b/src/cpp/server/platform/metrics_linux.cpp#L150-L181).

AMD SMI's process API is another point projection. Its own reference warns that
the sum of process memory is not expected to equal total device use; see
[`amdsmi_get_gpu_process_list`](https://rocm.docs.amd.com/projects/amdsmi/en/latest/reference/amdsmi-py-api.html#amdsmi-amdsmi-get-gpu-process-list).

## Trace and profiler candidates

### AMDGPU tracepoints and trace transport

Linux 7.1.8 exposes `amdgpu_bo_create` and `amdgpu_bo_move`, but their records do
not carry the PCI device, DRM client id, or complete shared-owner set. More
importantly, the driver calls `amdgpu_bo_move` from a notification explicitly
issued before the move happens, so the event is not proof that the requested
placement became the committed GTT state. See the exact stable sources for
[`amdgpu_trace.h`](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/gpu/drm/amd/amdgpu/amdgpu_trace.h?h=v7.1.8)
and
[`amdgpu_object.c`](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/gpu/drm/amd/amdgpu/amdgpu_object.c?h=v7.1.8).

The omission is semantic, not merely a missing field. A failed GTT allocation
can increment and then roll back the manager counter without a BO-create or
move event. During a move the destination resource is already charged before
`amdgpu_bo_move`, while the old resource is freed afterward, so the events
cannot reconstruct the true transient total. See
[`amdgpu_gtt_mgr.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/amd/amdgpu/amdgpu_gtt_mgr.c#L128-L182)
and
[`amdgpu_ttm.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c#L538-L571).

The generic `gpu_mem_total` tracepoint has alloc, free, import, and unimport
semantics, but AMDGPU does not emit it at the audited revision. Its PID field
would still not supply a stable owner generation or atomic subscription
baseline. See
[`gpu_mem.h`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/include/trace/events/gpu_mem.h#L16-L52).

Tracefs can expose overwrites and dropped records through each CPU buffer's
statistics, but its buffers are per-CPU. Those counters establish transport loss
for the events that were enabled; they cannot reveal a resource mutation for
which no complete event exists. See the kernel's
[`ftrace` ring-buffer interface](https://docs.kernel.org/trace/ftrace.html#the-directory-structure).
Perf can similarly emit `PERF_RECORD_LOST` for a non-overwrite ring, while its
ordering remains per-CPU. See the
[`perf ring-buffer`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/Documentation/userspace-api/perf_ring_buffer.rst#L478-L491).

BPF ring buffers improve cross-CPU reservation ordering and make a failed
reservation observable to a program that records the failure. They do not add
missing event semantics. Kernel tracing documentation also states that
tracepoints and kprobe attachment locations are not stable ABI, and tracing
programs that walk internal structures remain tied to those internals. See
[`BPF ring buffer`](https://docs.kernel.org/bpf/ringbuf.html) and
[`BPF Design Q&A`](https://docs.kernel.org/bpf/bpf_design_QA.html#q-are-tracepoints-part-of-the-stable-abi).

The actual sysfs counter changes in `ttm_resource_init()` and
`ttm_resource_fini()` under the TTM LRU lock. Probing those internal functions
could support an exact-kernel experiment, but neither function supplies the
stable atomic subscription/checkpoint boundary or complete DRM-client ownership
required here. A probe's ring sequence also need not equal the counter's lock
order unless the kernel emits the sequence at the mutation itself. See
[`ttm_resource.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/ttm/ttm_resource.c#L333-L387).

### ROCprofiler and KFD events

ROCprofiler memory-allocation tracing records selected HSA allocation APIs used
under `hipMalloc`; frees record an address but not the freed allocation size.
This is a userspace/runtime allocation trace, not a journal of committed TTM GTT
placement. See AMD's
[`Memory allocation trace`](https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/docs-7.14.0/how-to/using-rocprofv3.html#memory-allocation-trace).

KFD tracing covers SVM migration, page faults, queue eviction/restoration, GPU
unmaps, and process lifecycle. The UAPI permits all-process delivery only with
superuser permission. It does not define GTT BO allocation/free events or a
device-wide `mem_info_gtt_used` mutation stream. ROCprofiler can report KFD
dropped-event records, but that proves completeness only for this narrower KFD
event set. See the kernel's
[`KFD SMI event UAPI`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/include/uapi/linux/kfd_ioctl.h#L860-L974)
and AMD's
[`rocprofv3 KFD options`](https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/docs-7.14.0/quick-reference/rocprofv3-cli-options.html).

## Enforcement candidates

### Device-memory cgroup

The cgroup v2 `dmem` controller is the closest upstream enforcement primitive.
For a driver-registered region, current Linux mainline exposes per-cgroup
`dmem.current`, `dmem.max`, and `dmem.peak`; charges are assigned to the current
task's dmem cgroup, retained with the TTM resource for its lifetime, and a limit
failure is reported before the resource is committed. See
[`cgroup-v2.rst`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/Documentation/admin-guide/cgroup-v2.rst#L2911-L2958),
[`TTM dmem charging`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/ttm/ttm_resource.c#L388-L460),
and
[`dmem_cgroup_try_charge()`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/kernel/cgroup/dmem.c#L695-L758).

AMDGPU does not register GTT with this controller. Linux 7.1.8 registers a dmem
region only in `amdgpu_vram_mgr_init()`; `amdgpu_gtt_mgr_init()` initializes its
TTM manager without a cgroup region. Current mainline retains the same boundary:
[`amdgpu_vram_mgr.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/amd/amdgpu/amdgpu_vram_mgr.c#L942-L986)
versus
[`amdgpu_gtt_mgr.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/amd/amdgpu/amdgpu_gtt_mgr.c#L370-L394).

A read-only Hatchery check at `2026-08-26T00:28:28Z` confirmed the deployed
interface:

```text
kernel=7.1.8-1-cachyos
cgroup.controllers=cpuset cpu io memory hugetlb pids rdma misc dmem
dmem.capacity=drm/0000:c6:00.0/vram 1073741824
dmem.current=drm/0000:c6:00.0/vram 1006202880
amdgpu.gtt.total=120259084288
amdgpu.gtt.used=1590456320
```

The exact 112 GiB GTT region has no dmem key. Linux 7.1.8 also lacks the newer
`dmem.peak` interface. This directly blocks both per-transaction GTT peak
retention and kernel-enforced GTT limits on the current host.

If AMDGPU later registers every GTT resource with dmem, a newly created cgroup
per transaction could provide a stable owner scope, a lifetime peak, and a hard
target limit. That would be materially stronger than sampling and is the most
credible alternate evidence boundary. It would still need proof for moves made
by kernel workers, imported/shared BOs, driver-internal allocations, external
domain use, cgroup migration, device reset, and host-memory-floor behavior. A
limit and peak alone do not satisfy the currently accepted dense-event contract.

### Device access, task control, and GPU isolation

The cgroup v2 device hook runs when a task attempts device-file access and can
deny that attempt. It does not provide memory accounting or a resource-event
cursor. A successful `open()` creates an open file description that remains
usable independently of the pathname, and UNIX-domain `SCM_RIGHTS` can pass a
reference to it to another process. Device namespaces and later access-policy
changes therefore cannot establish ownership of already-open DRM or KFD
resources. See the kernel's
[`Device controller`](https://docs.kernel.org/admin-guide/cgroup-v2.html#device-controller),
[`open(2)`](https://man7.org/linux/man-pages/man2/open.2.html), and
[`unix(7)`](https://man7.org/linux/man-pages/man7/unix.7.html).

KFD applies device-cgroup authorization through the matching DRM render node,
so it has the same admission ceiling. Existing process-device data is expressly
a condition that prevents a new binding mode, and ordinary unbinding follows
process death or device removal rather than a later cgroup denial. See
[`kfd_priv.h`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/amd/amdkfd/kfd_priv.h#L1616-L1633),
[`kfd_device.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/amd/amdkfd/kfd_device.c#L1554-L1567),
and
[`kfd_process.c`](https://github.com/torvalds/linux/blob/45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229/drivers/gpu/drm/amd/amdkfd/kfd_process.c#L1899-L1941).

DRM render nodes deliberately grant rendering access without DRM-master
authentication and omit the DRM-master concept. DRM leases apply to display
resources owned by a primary-node master, not to the independent render clients
used by ROCm. See
[`Render nodes`](https://docs.kernel.org/gpu/drm-uapi.html#render-nodes) and
[`DRM display resource leasing`](https://docs.kernel.org/gpu/drm-uapi.html#drm-display-resource-leasing).

These controls can reduce new competing access after a separately proven clean
baseline, but Hatchery already has long-lived graphics/render clients and the
controls neither close their existing descriptors nor retain their GTT changes.

A fresh process cgroup gives the workload a stable launch scope.
`cgroup.freeze` can report when its member tasks have stopped, but defines no GPU
queue-drain or buffer-residency semantics. Submitted GPU work can continue and
resident objects can survive the userspace stop; unrelated and kernel clients
also remain outside the cgroup. See
[`cgroup.freeze`](https://docs.kernel.org/admin-guide/cgroup-v2.html#core-interface-files).

AMDGPU process isolation serializes processes on selected engines and clears
selected shader state. It is not a memory partition or GTT ownership lease; see
[`AMDGPU process isolation`](https://docs.kernel.org/gpu/amdgpu/process-isolation.html).
DMA-buf makes the ownership problem more explicit: a buffer is represented by a
file descriptor designed for sharing across clients and drivers, while its sync
operations coordinate access rather than make one client the exclusive owner.
See the kernel's [`DMA-buf sharing framework`](https://docs.kernel.org/driver-api/dma-buf.html).

AMDGPU debugfs can evict GTT, trigger recovery, and expose diagnostic state, but
debugfs has no stable ABI guarantee. Those destructive or point-in-time controls
neither retain a mutation history nor attribute every mutation. See
[`AMDGPU debugfs`](https://docs.kernel.org/gpu/amdgpu/debugfs.html) and the
[`debugfs`](https://docs.kernel.org/filesystems/debugfs.html) interface policy.

Full-function assignment or controlled driver rebinding could instead define a
different, whole-device qualification topology. It does not prove the accepted
interval for this shared integrated-GPU cell: the guest or rebound driver still
creates unattributed GTT demand, and the topology itself supplies no mutation
ledger, target projection, or history-loss detector.

## What would clear the mutation-complete interval gate

One of these two kernel/driver authorities would need to exist and be qualified
for the exact deployment before claiming mutation-complete interval evidence:

1. **Event authority.** AMDGPU or TTM exposes a stable device-bound source epoch,
   an atomic subscribe-plus-current-GTT checkpoint, one globally ordered dense
   sequence value for each committed GTT counter mutation, the new device total,
   complete immutable owner attribution with defined shared/imported-BO
   semantics, retained oldest/latest cursors, explicit gap/overflow/reset
   detection, and atomic checkpoint-plus-drain.
2. **Enforcement authority.** AMDGPU registers complete GTT charging with dmem;
   the profiling transaction owns a fresh cgroup before any workload process or
   allocation exists; per-cgroup GTT current, peak, and max cover all target
   effects; every non-target actor is either prevented from changing the domain
   or represented by an equally complete domain boundary; and process exit,
   cgroup teardown, reset, and release are explicitly drained and verified.

The second route could justify an enforced-bound policy without replaying every
mutation, but it must not be represented as satisfying the existing dense-event
contract. Neither route is required for the narrower differential
retained-footprint boundary selected for the first release.

## Decision

[Choose the first-release whole-interval evidence boundary](https://github.com/nisavid/lemonade/issues/139)
keeps two claims separate for the first release:

- controlled A-B-A global GTT points may authorize an exact-fingerprint
  differential retained-footprint calibration when the Server owns the
  intervention and every identity, stability, noise, uncertainty, and release
  check passes;
- the complete load and request transient envelope remains a separate cataloged
  conservative bound, and a mutation-complete source remains an optional
  stronger future authority.

[Implement the isolated residency profiling transaction](https://github.com/nisavid/lemonade/issues/120)
therefore retains activation-capable scope but splits its evidence model. The
differential path may produce only the retained GTT component; the transaction
may activate it only when a separately complete transient envelope and every
other required claim family close the admission manifest. The existing dense
interval path remains a stronger alternate source, while the point adapter can
never serve as that complete interval capture authority.

The differential retained result is method-specific component evidence, not a
confidence class. Under a catalog-preauthorized local-instantiation rule, the
completed composite manifest may carry `calibrated_instance` confidence only
after the separate transient envelope and every non-footprint claim close. The
overlay does not change shared catalog confidence, grants no portable predictor
authority, and cannot generalize to another model or operation.

No current fact supports converting point polling, AMDGPU tracepoints,
ROCprofiler records, or device-access isolation into an activation cursor. The
differential method instead derives one explicitly narrower retained effect
from a controlled intervention and never claims to have observed every mutation.
