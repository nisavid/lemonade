# Portable residency signals and ownership

Research snapshot: upstream Lemonade commit
[`a80e5dfae64e7595ed98bd9ee4ad020a43037ea1`](https://github.com/lemonade-sdk/lemonade/tree/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1),
2026-08-11. This note answers the platform-research question for the accepted
[residency capability model](../../CONTEXT.md#residency-capability-level); it does
not change that policy.

## Scope and conclusion

Current upstream GPU support comprises these platform families:

- Apple Silicon Metal on macOS.
- NVIDIA CUDA on Windows and Linux.
- AMD ROCm on Windows and Linux, including Linux Strix Halo and other iGPU
  paths.
- Vulkan on Windows and Linux, including AMD iGPU/dGPU and ARM64 GPU paths and
  experimental vendor-neutral “Vulkan-capable GPU” paths.
- A Linux `llamacpp:system` package whose GPU provider is not specified by the
  recipe.

Those are the families in the upstream backend matrix; CPU and NPU-only rows are
outside this GPU-residency question. The matrix also establishes that GPU
support is modality-wide rather than limited to text generation.
([upstream text-generation matrix](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/README.md#L150-L198),
[upstream GPU modality rows](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/README.md#L200-L341))

No one portable scalar is an adequate residency signal. A safe provider must
report an identified affected-domain set, the scope of each measurement, a
sample time or event generation, and independent host-safety state. Shared and
unified memory make device headroom and host headroom constraints on the same
physical bytes, not additive pools. These conclusions follow from the
platform-specific contracts below.

The primary-source evidence supports modeled implementations on Linux AMD
dGPU, Linux NVIDIA dGPU, Windows NVIDIA dGPU, and—after adding backend telemetry—
Apple Metal. Linux cgroup v2 and Windows Job Objects provide modeled paths to
strong descendant ownership. Generic Vulkan, shared-memory GPUs, the opaque
Linux `system` package, and macOS recovery remain fallback-only until the listed
integration and physical tests close their gaps. “Modeled” here is an evidence
ceiling, not a claim that current Lemonade already implements or validates the
adapter.

## What current upstream actually measures

Upstream exposes a single `get_global_vram_usage_pct()` value and polls it every
two seconds while global auto-eviction is enabled.
([interface](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/include/lemon/system_info.h#L160-L162),
[poller](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/server/global_vram_monitor.cpp#L10-L59))
The scalar has no adapter identity, domain identity, measurement scope, sample
timestamp, generation, or health reason.

Its sources have materially different meanings:

- `nvidia-smi` supplies used/total memory for every visible NVIDIA GPU, and
  upstream retains only the highest ratio.
- Linux AMD sysfs supplies per-card VRAM and, for an APU, GTT. Upstream compares
  a combined VRAM+GTT ratio with a preferred-pool ratio and again retains only
  the highest ratio.
- Windows DXGI is consulted only if NVIDIA produced no ratio. It discards all
  integrated adapters when any discrete adapter exists and computes
  `1 - Budget / DedicatedVideoMemory` (or shared memory), without using
  `CurrentUsage`.
- macOS has no branch and therefore returns “source unavailable” unless the
  NVIDIA command unexpectedly supplied a result.

These are direct properties of the current implementation.
([implementation](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/server/system_info.cpp#L4153-L4341))
DXGI defines `CurrentUsage` as the calling application's usage and `Budget` as
the OS-provided target for that application, so the Windows calculation is a
budget-shrinkage proxy—not all-process used memory—and it measures the daemon's
budget rather than a backend child's current allocation.
([DXGI contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info))

The scalar consequently cannot prove which resident would free the constrained
domain, whether a different adapter is healthy, whether host memory is safe, or
whether the sample still belongs to the current device/configuration
generation. In particular, mixed NVIDIA/AMD Windows systems never incorporate
the AMD DXGI result when `nvidia-smi` succeeds, and the “any dGPU hides all
iGPUs” rule loses shared-domain pressure.
([implementation](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/server/system_info.cpp#L4153-L4341))

## Platform signal contracts

### Linux AMD: ROCm and Vulkan

Linux amdgpu sysfs exports byte counts for total and used VRAM and GTT on each
DRM device. Those are device-wide pool measurements and can be sampled whenever
the files remain readable.
([kernel amdgpu memory attributes](https://docs.kernel.org/6.6/gpu/amdgpu/driver-misc.html#memory-usage))
AMD SMI additionally exposes per-device BDF and UUID identifiers, VRAM totals
and usage, and per-process GTT/CPU/VRAM usage; its documentation warns that the
sum of per-process values need not equal device totals.
([AMD SMI device and process APIs](https://rocm.docs.amd.com/projects/amdsmi/en/latest/reference/amdsmi-py-api.html))
AMD SMI currently documents Linux bare-metal and Linux guest support, not
Windows support.
([AMD SMI support matrix](https://rocm.docs.amd.com/projects/amdsmi/en/latest/install/install.html#amd-smi-library))

HSA/ROCm memory pools report location, size, accessibility, and whether the
agent is an APU. The runtime also states that a pool is a preferential
partition: binding to that physical location may be impossible or may change
because of system policy and state. Capacity is therefore a domain constraint,
not proof of where every backend allocation resides.
([ROCr memory-pool model](https://rocm.docs.amd.com/projects/ROCR-Runtime/en/develop/api-reference/api.html#memory-pools))

For host safety, `/proc/meminfo` `MemAvailable` estimates memory available for
new applications without swapping, including reclaim and low-watermark effects.
([Linux `/proc/meminfo`](https://docs.kernel.org/6.13/filesystems/proc.html#meminfo))
PSI reports current CPU, memory, and I/O stalls at system and cgroup scope,
including pollable thresholds whose windows may range from 50 ms to one second.
([Linux PSI](https://docs.kernel.org/6.10/accounting/psi.html))
Neither interface carries a GPU adapter identity, so the provider must correlate
DRM/HSA identity separately and treat host signals as additional constraints.

**Guarantee:** per-DRM-card VRAM/GTT counters, stable device identifiers when
the selected API provides them, and independent host availability/pressure are
available on Linux AMD.

**Inference:** the backend recipe, selected device, and allocation policy
determine whether VRAM, GTT, or both are affected. On an APU the same physical
system memory can satisfy a GPU allocation and reduce host availability, so a
planner must constrain the shared allocation by both signals without adding
their capacities.
([Vulkan UMA guidance](https://docs.vulkan.org/guide/latest/memory_allocation.html#unified-memory-architecture))

**Validation need:** record before/load/idle/unload deltas for every relevant
backend and modality. Hatchery must specifically verify the stated GTT-only
weight-and-cache behavior under ROCm and Vulkan, including a co-resident process
that creates host and GTT pressure. Other APU layouts remain fallback-only until
their placement is demonstrated.

### Linux NVIDIA: discrete CUDA and integrated/shared CUDA

NVML exposes per-device memory total, free, reserved, and used, with device
handles selectable by index, serial, PCI bus ID, or UUID.
([NVML device queries](https://docs.nvidia.com/deploy/nvml-api/group__nvmlDeviceQueries.html),
[memory fields](https://docs.nvidia.com/deploy/nvml-api/structnvmlMemory__v2__t.html))
NVIDIA recommends UUID or PCI bus ID rather than enumeration index because
indices are not stable between reboots. `nvidia-smi` also distinguishes discrete
framebuffer memory from system memory used by integrated GPUs, and notes that
per-process GPU memory is unavailable in Windows WDDM mode.
([NVIDIA SMI identifiers and process accounting](https://docs.nvidia.com/deploy/nvidia-smi/index.html))

Upstream already records NVIDIA UUID/index/capacity, prefers UUID for
`CUDA_VISIBLE_DEVICES`, and may expose every compatible same-architecture GPU to
one backend when all devices match. An admission provider must therefore map a
resident to an affected set of one or more UUID domains rather than assume one
process means one GPU.
([upstream GPU identity](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/include/lemon/system_info.h#L27-L34),
[upstream CUDA selection](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/server/system_info.cpp#L2282-L2372),
[backend environment](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/server/backends/llamacpp/llamacpp_server.cpp#L513-L530))

NVML event sets cover enumerated hardware, clock, power, and MIG events, but do
not define a memory-budget or memory-pressure event. Memory use therefore needs
polling, with a provider-owned timestamp/generation and explicit error state.
([NVML event contract](https://docs.nvidia.com/deploy/nvml-api/group__nvmlEvents.html))
CUDA's unified-memory behavior varies with OS, kernel, GPU attributes, and
interconnect; not every platform permits all system memory to be used
implicitly. Integrated/shared NVIDIA combinations therefore need physical
placement validation rather than inheriting the discrete-VRAM model.
([CUDA memory model](https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/understanding-memory.html#unified-memory))

**Guarantee:** Linux discrete NVIDIA has identifiable device-wide capacity/use
counters plus independent Linux host signals.

**Inference:** attribution of released bytes to a backend is a before/after
model that must account for backend multi-GPU selection; no NVIDIA source here
proves that model weights and cache use the same domain.

**Validation need:** exercise one-GPU, same-architecture multi-GPU, and mixed-
architecture selection. Integrated NVIDIA/ARM64 paths stay fallback-only until
memory placement, device/accounting identity, and host-floor interaction are
shown on supported hardware.

### Linux and Windows generic Vulkan, including `llamacpp:system`

Vulkan exposes physical-device heaps and memory types. Discrete systems usually
have a device-local heap separate from host memory; UMA systems commonly expose
one heap whose device-local memory types may also be host-visible.
([Vulkan memory heaps and types](https://docs.vulkan.org/spec/latest/chapters/memory.html),
[Vulkan discrete/UMA guidance](https://docs.vulkan.org/guide/latest/memory_allocation.html))
When `VK_EXT_memory_budget` is supported, each physical-device heap has an
implementation-estimated current-process usage and budget. The values may vary
with system load, are not a global all-process usage signal, and are intended to
be polled at an application-chosen interval.
([Vulkan memory-budget extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_memory_budget.html))
Vulkan device UUIDs correlate a physical device across processes; on Windows,
`deviceLUID` can match a DXGI adapter LUID.
([Vulkan physical-device identity](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceIDProperties.html))

The useful Vulkan telemetry exists inside the backend process that owns the
Vulkan device and allocations. Lemonade therefore needs a backend telemetry
contract that returns the selected device UUID/LUID, heap identities,
`heapUsage`, `heapBudget`, support flags, and sample generation. Without that
contract, an OS-level host signal cannot attribute pressure or expected recovery
to a Vulkan resident.

The Linux `llamacpp:system` recipe promises only a GPU-capable system package;
it does not establish the vendor, selected physical device, memory provider, or
telemetry contract.
([upstream system-package row](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/README.md#L150-L169))
It is fallback-only until runtime discovery returns a descriptor matching a
known provider. A recognized Vulkan device with the extension and a correlated
OS provider can be modeled; an unknown or uncorrelatable device cannot safely
use capacity automation.

### Windows: DXGI, CUDA, ROCm, and Vulkan

DXGI identifies adapters with a locally unique LUID and reports dedicated video
memory and shared system memory in its adapter description.
([DXGI adapter description](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_adapter_desc))
D3D12 describes discrete local memory as VRAM and non-local memory as system
memory, while UMA adapters have no non-local segment and expose system memory as
local. Available physical memory can vary during the process lifetime.
([Windows memory topologies](https://learn.microsoft.com/en-us/windows/win32/direct3d12/memory-management-strategies))

`QueryVideoMemoryInfo` returns an application-scoped `CurrentUsage` and an
OS-provided `Budget`; budget can change because of other processes and context
switches. Microsoft recommends checking both values and making reservations
conservatively because exceeding the budget can cause stutter, allocation
failure, or device removal.
([query contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info),
[budget behavior](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-1-4-improvements))
DXGI can set an event when the adapter budget changes, giving a genuine signal
generation that should trigger a fresh query rather than replace the query.
([budget-change event](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-registervideomemorybudgetchangenotificationevent))

`GlobalMemoryStatusEx` supplies a call-time system memory snapshot including
available physical bytes, and Windows provides system-wide low/high memory
resource notification events with an intentional neutral range.
([host-memory snapshot](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex),
[host-memory notification](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-creatememoryresourcenotification))

**Guarantee:** DXGI provides adapter topology, an application budget, and a
budget-change generation; NVIDIA can separately provide device-wide capacity
and use when NVML/SMI works.

**Gap:** AMD SMI does not document Windows support, DXGI queried by `lemond`
does not measure a separate backend child's `CurrentUsage`, and Vulkan budget is
available only inside the backend. Windows ROCm and non-NVIDIA Vulkan are thus
fallback-only until backend telemetry is implemented and correlated by
LUID/UUID. Windows NVIDIA dGPU is modeled when UUID selection and NVML totals
match the backend, but its WDDM per-process attribution and DXGI budget response
still require physical tests.

**Validation need:** test NVIDIA-only, AMD-only, UMA, and mixed AMD+NVIDIA
systems; change another application's allocation and confirm adapter-local
budget generations, child-versus-daemon accounting, host notifications, and
the expected bytes released by each backend. Current upstream's NVIDIA-first
scalar must not be used as evidence that the AMD domain is healthy.

### macOS Apple Silicon Metal

Metal reports whether a device has unified memory, an approximate recommended
maximum working-set size, and the current size of resources allocated by the
calling process. The recommended maximum is a performance guideline rather
than a physical capacity guarantee.
([Metal memory properties](https://developer.apple.com/documentation/metal/mtldevice/recommendedmaxworkingsetsize))
`registryID` identifies the same GPU across task boundaries.
([Metal registry identity](https://developer.apple.com/documentation/metal/mtldevice/registryid))

Dispatch memory-pressure sources report normal, warning, and critical system
memory-pressure transitions; the critical contract tells an application to
release as much memory as possible.
([memory-pressure source](https://developer.apple.com/documentation/dispatch/dispatchsourcememorypressure),
[critical pressure](https://developer.apple.com/documentation/dispatch/dispatch_memorypressure_critical))
`os_proc_available_memory()` is advisory, must not be cached, reports the
calling application's allocation allowance rather than physical free memory,
and returns zero outside an app process. It is not established as a quantitative
host floor for `lemond` without validation.
([available-memory contract](https://developer.apple.com/documentation/os/os_proc_available_memory))

**Guarantee:** macOS has a unified-memory topology flag, cross-process device
identity, backend-process allocation/working-set properties, and event-driven
system pressure state.

**Gap:** current upstream has no macOS global-memory provider, Metal allocation
telemetry belongs to the backend process, and the researched public contract
does not establish a quantitative daemon host-floor source. Admission is
fallback-only until backend telemetry and a conservative quantitative host
constraint are validated. Event-triggered measured-pressure reclamation has a
modeled path, but only after it is tied to fresh Metal allocation state and
physical tests.

## Subprocess containment and recovery

The accepted policy requires complete child-tree containment or crash-consistent
ownership that can survive daemon interruption. A PID alone is not ownership
proof.

### Linux

Cgroup v2 provides a hierarchical containment unit. `cgroup.events` reports a
pollable populated state, `memory.current` and `memory.pressure` cover a cgroup
and its descendants, and `cgroup.kill` terminates all descendants while handling
concurrent forks and migrations.
([cgroup v2 interface](https://docs.kernel.org/admin-guide/cgroup-v2.html))
`clone3(CLONE_INTO_CGROUP)` places a child in a target cgroup atomically at
creation, and `CLONE_PIDFD` returns a stable live-process handle.
([clone3 flags](https://man7.org/linux/man-pages/man2/clone.2.html))
Pidfds avoid PID-reuse races for observation and signaling, although a pidfd is
a live kernel handle rather than durable post-crash identity.
([pidfd creation](https://www.man7.org/linux/man-pages/man2/pidfd_open.2.html),
[pidfd signaling](https://www.man7.org/linux/man-pages/man2/pidfd_send_signal.2.html))
A delegated systemd service/scope is another supported way to obtain a managed
cgroup subtree, subject to the delegation contract and single-writer rule.
([systemd cgroup delegation](https://systemd.io/CGROUP_DELEGATION/))

Current upstream instead forks, has the immediate child set
`PR_SET_PDEATHSIG(SIGTERM)`, and later signals only the recorded PID.
([upstream Linux launcher](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/server/utils/platform/process_linux.cpp#L138-L205))
The kernel contract says a parent-death signal is cleared in forked children and
is not delivered when the relevant parent was already dead before `prctl`; it
therefore cannot guarantee complete descendant cleanup or close the spawn race.
([`PR_SET_PDEATHSIG` contract](https://man7.org/linux/man-pages/man2/PR_SET_PDEATHSIG.2const.html))

**Modeled recovery authority:** atomically launch every backend into a unique,
delegated cgroup; durably prepare the launch nonce/cgroup identity and maximum
claims first; retain a pidfd while live; use `cgroup.kill` and wait for
`populated=0` at unload/recovery. Physical validation must cover daemon SIGKILL,
fork storms, PID reuse, permission loss, and systems without usable cgroup
delegation. If atomic containment or equivalent crash-consistent ownership is
unavailable, lifecycle automation falls back or refuses.

### Windows

A Job Object groups processes; by default, child processes join the parent's job
unless breakaway is allowed. `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` terminates all
associated processes when the last job handle closes.
([Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects))
`PROC_THREAD_ATTRIBUTE_JOB_LIST` can assign a new process to one or more jobs as
part of process creation on supported Windows versions, avoiding an
uncontained-spawn interval.
([process-creation job attribute](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute))
Windows exposes process creation time, which can join a launch nonce and job
identity as a non-reusable runtime check rather than trusting PID alone.
([process times](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes))

Current upstream uses plain `CreateProcessA` and `TerminateProcess` on only the
returned process handle; it creates no Job Object.
([upstream Windows launcher](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/server/utils/platform/process_windows.cpp#L174-L353))

**Modeled recovery authority:** prepare the durable record; create a unique Job
Object with kill-on-close and no breakaway; assign it atomically through the
creation attribute; record job identity plus process creation time; close and
verify the job during recovery. Physical validation must cover daemon crash,
backend grandchildren, nested-job compatibility, handle inheritance/leaks, and
reopening/reconciling a named job after restart. Without atomic job membership,
automation falls back or refuses.

### macOS

`posix_spawn` can put the child in a process group atomically, and `killpg` can
signal that group.
([spawn process-group attribute](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/posix_spawnattr_getpgroup.3.html),
[`killpg`](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/killpg.2.html))
Kqueue/dispatch can monitor process events such as exit, fork, and exec for a
known PID while the monitor remains live.
([kqueue process filter](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kqueue.2.html),
[dispatch process source](https://developer.apple.com/documentation/dispatch/dispatchsourceprocess))
Current upstream calls `posix_spawn` without creating a process group and later
signals only the recorded PID.
([upstream macOS launcher](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/server/utils/platform/process_macos.cpp#L84-L210))

**Gap:** the public primary interfaces researched here establish useful live
process-group cleanup and event observation, but not a Windows-Job- or Linux-
cgroup-equivalent guarantee that an entire descendant tree remains contained
and is reclaimed after daemon crash. Process groups and PIDs are reusable, and
a descendant may create a new session. macOS recovery is therefore
fallback-only pending physical proof of a launchd/job-based authority or a
crash-consistent ownership design that meets the accepted barrier. This is an
evidence gap, not a claim that no suitable macOS mechanism exists.

## Recommended capability profile

Each cell below is per operation and affected-domain set. It is the maximum
level justified by current primary evidence after the described adapter is
implemented; unhealthy, stale, uncorrelated, or missing required signals reduce
the effective mode to the named fallback or refusal.

| Supported family | Admission | Measured-pressure reclamation | Recovery authority | Effective fallback and advancement test |
| --- | --- | --- | --- | --- |
| Linux AMD dGPU, ROCm/Vulkan | **Modeled** from per-card VRAM/GTT totals and use plus backend device mapping. | **Modeled** from fresh pool counters, `MemAvailable`, and PSI. | **Modeled** cgroup v2 design. | Fall back/refuse when DRM identity, cgroup authority, or host signals are unavailable. Advance to validated through load/idle/unload attribution, competing-process pressure, and crash injection. ([amdgpu](https://docs.kernel.org/6.6/gpu/amdgpu/driver-misc.html#memory-usage), [cgroup v2](https://docs.kernel.org/admin-guide/cgroup-v2.html)) |
| Linux AMD APU/iGPU, including Strix Halo | **Fallback-only** generally; **modeled on Hatchery** only after its GTT-only footprint claim is physically reproduced. | **Modeled** architecture, but fallback-only for a backend whose allocation domain is unproven. | **Modeled** cgroup v2 design. | Treat GTT/device and host floors as constraints on shared bytes. Validate every backend/modality and co-resident host pressure before broadening the cell. ([ROCr pools](https://rocm.docs.amd.com/projects/ROCR-Runtime/en/develop/api-reference/api.html#memory-pools), [Vulkan UMA](https://docs.vulkan.org/guide/latest/memory_allocation.html#unified-memory-architecture)) |
| Linux NVIDIA discrete CUDA | **Modeled** from UUID-scoped NVML totals/use and exact CUDA device mapping. | **Modeled** by polling NVML with sample generation plus Linux host signals. | **Modeled** cgroup v2 design. | Refuse capacity automation when UUID mapping or containment is ambiguous. Validate single-, same-architecture multi-, and mixed-GPU selection plus descendant cleanup. ([NVML](https://docs.nvidia.com/deploy/nvml-api/group__nvmlDeviceQueries.html), [upstream selection](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/src/cpp/server/system_info.cpp#L2282-L2372)) |
| Linux NVIDIA integrated/shared CUDA | **Fallback-only** pending placement and capacity semantics. | **Fallback-only** pending correlation of device and host pressure. | **Modeled** cgroup v2 design. | Never add integrated “GPU memory” to host capacity. Validate on each supported integrated family before enabling automation. ([NVIDIA SMI](https://docs.nvidia.com/deploy/nvidia-smi/index.html), [CUDA memory](https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/understanding-memory.html#unified-memory)) |
| Linux known generic Vulkan device | **Modeled** only with backend `VK_EXT_memory_budget` telemetry, UUID correlation, and a compatible OS/global provider; otherwise fallback-only. | **Modeled** only with fresh backend heap state plus OS pressure; otherwise fallback-only. | **Modeled** cgroup v2 design. | Unknown extension, device, heap, or attribution means no capacity automation. Validate each vendor/device/provider combination. ([budget extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_memory_budget.html), [device identity](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceIDProperties.html)) |
| Linux `llamacpp:system` with unknown GPU provider | **Fallback-only.** | **Fallback-only.** | **Modeled** containment does not cure unknown memory domains. | Runtime must identify and correlate a known provider before inheriting its cells; otherwise preserve manual load/unload and refuse capacity automation. ([upstream matrix](https://github.com/lemonade-sdk/lemonade/blob/a80e5dfae64e7595ed98bd9ee4ad020a43037ea1/README.md#L150-L169)) |
| Windows NVIDIA discrete CUDA | **Modeled** from matching NVIDIA UUID totals/use and backend selection. | **Modeled** from polled NVIDIA use plus DXGI budget generation and host safety; per-child attribution remains a test requirement. | **Modeled** Job Object design. | Fall back/refuse on WDDM attribution ambiguity, missing NVML, or non-atomic job membership. Validate NVIDIA-only and mixed-vendor systems. ([NVIDIA SMI](https://docs.nvidia.com/deploy/nvidia-smi/index.html), [DXGI](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info), [Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)) |
| Windows AMD ROCm or any discrete Vulkan | **Fallback-only** until child telemetry is correlated to DXGI LUID. | **Fallback-only** until backend heap use/budget, DXGI budget generation, and host signals are joined. | **Modeled** Job Object design. | DXGI daemon `CurrentUsage` and budget shrinkage alone are insufficient. Validate backend-child accounting and released bytes. ([Vulkan identity](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceIDProperties.html), [DXGI budget](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-1-4-improvements)) |
| Windows UMA/shared GPU, any supported vendor | **Fallback-only.** | **Fallback-only.** | **Modeled** Job Object design. | Model adapter and host constraints on the same bytes, and validate child budget/current-use behavior plus low-memory response before advancement. ([Windows topology](https://learn.microsoft.com/en-us/windows/win32/direct3d12/memory-management-strategies), [host memory](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex)) |
| macOS Apple Silicon Metal | **Fallback-only** until backend allocation telemetry and a quantitative host constraint are validated. | **Modeled** event-driven response after fresh Metal state is joined to dispatch pressure. | **Fallback-only** pending a proven complete-tree authority. | The recovery prerequisite keeps automatic lifecycle operations in fallback/refusal even if the signal cell advances. Validate pressure, released bytes, and crash recovery on Apple Silicon. ([Metal](https://developer.apple.com/documentation/metal/mtldevice/recommendedmaxworkingsetsize), [pressure](https://developer.apple.com/documentation/dispatch/dispatchsourcememorypressure)) |

The recovery cell is a prerequisite, not an independent excuse to proceed. A
platform with modeled memory signals but fallback-only ownership must keep the
affected automatic lifecycle operation in fallback/refusal until recovery
authority is established.

## Provider and validation handoff

Every platform adapter should return one immutable snapshot containing:

1. Provider version and health; monotonic sample time; configuration/device
   generation; event generation where available; maximum sample age.
2. Stable physical-device identity (UUID, PCI BDF, DXGI LUID, or Metal registry
   ID), selected backend identity, and the complete affected-domain set.
3. For each domain: topology (`dedicated`, `shared`, or `unified`), capacity or
   budget, measured use, measurement scope (`system`, `process`, or estimate),
   confidence, and resident footprint estimates.
4. Independent host availability and pressure, with an explicit relationship to
   shared/unified device bytes.
5. Recovery-authority identity and current containment/recovery health.

The planner must reserve against one coherent snapshot, re-check its generation
before mutation, and refuse or fall back on a missing/stale member rather than
combining unrelated maxima. Adapter identity requirements follow the UUID/LUID/
registry contracts above; freshness follows native events where they exist and
timestamped polling elsewhere.

Required physical campaigns before any cell becomes validated:

- Hatchery: prove ROCm and Vulkan weight/cache placement, expected GTT recovery,
  and host-floor behavior under synthetic and real co-resident pressure.
- Linux: cover AMD dGPU/APU, NVIDIA discrete/integrated, generic Vulkan, opaque
  `system`, multi-GPU selection, hot-reset/error paths, unreadable/stale sensors,
  and cgroup permission/delegation failure.
- Windows: cover NVIDIA, AMD, generic Vulkan, UMA, mixed vendors, multiple GPUs,
  WDDM accounting, budget-change ordering, device loss, and Job Object crash and
  descendant tests.
- macOS: cover Metal allocation/working-set deltas, memory-pressure transitions,
  a quantitative host-floor candidate, multi-device identity if exposed, and a
  complete-tree crash-recovery design.
- Every OS: inject failure before spawn, immediately after process creation,
  during load, during unload, and during daemon restart; verify no signal is sent
  to an uncertain/reused identity and no claim is released until the entire
  resource tree is gone.

Until those tests pass, the table's modeled cells remain modeled and its
fallback-only cells remain explicit capability gaps.
