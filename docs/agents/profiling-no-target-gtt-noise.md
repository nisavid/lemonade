# No-target GTT noise producer and consumer procedure

Use this procedure to create or consume one boot-scoped, immutable no-target `N_gtt` result through the public residency profiling interfaces. It validates trace structure and content bindings. It does not collect observations or authenticate who made them.

## Authority and entry conditions

The Server caller owns the exclusive deployment profiling gate, queued competing Lemonade work, target absence during production, process containment, authentic observation source, and durable journal state. Begin only when the caller has:

- excluded the declared target and other unaccounted GTT clients from the no-target trace;
- bound the lowercase 64-hex deployment identity and boot epoch, exact device and topology, kernel and driver, global counter source and revision, trace-era counter continuity epoch, campaign contract, this reviewed procedure revision, and non-target background inventory;
- selected a measured read/skew uncertainty `U` in bytes;
- provided a deterministic 15-minute observation trace from the authoritative global `mem_info_gtt_used` counter; and
- established that the requested calibration revision is fresh in the existing Server journal.

Set `procedure_revision_sha256` to the lowercase hexadecimal SHA-256 of the exact raw file-content bytes stored for `docs/agents/profiling-no-target-gtt-noise.md` at the applicable reviewed commit. Hash those bytes without Git object framing or content transformation; do not hash rendered Markdown, a commit identifier, URL, or review receipt.

For each profiling invocation, the Server caller selects the applicable reviewed commit: one immutable commit whose clean affected review covers this procedure and the corresponding profiling implementation at that same revision. Do not substitute a moving branch tip or evidence reviewed at another revision. The component accepts values and digests as input; it does not attest the observer, enforce the gate, inspect live hardware, or persist journal decisions.

The supported trace types and result interfaces are declared in `src/cpp/include/lemon/residency/profiling_noise.h`. The frozen consumer types are declared in `src/cpp/include/lemon/residency/profiling_differential_input.h`.

## Produce the immutable noise result

1. Build one `ProfilingNoTargetGttTrace` with an exact 15-minute `[started_at, exact_end)` boundary and the complete `ProfilingNoiseBindings`, including the counter continuity epoch observed for the whole trace.
2. Schedule nominal 50 ms acquisitions, with no more than 36,000 readings in the trace. Every scheduled timestamp and read initiation must remain ordered inside the trace, the first schedule must equal `started_at`, and neither consecutive schedule nor consecutive read initiation may be more than 100 ms apart. The trace must reach its exact end without a gap above 100 ms.
3. For every acquisition, record `scheduled_at`, `read_started_at`, `read_finished_at`, a successful global GTT byte value, and the bindings rechecked for that acquisition. A failed read, reversed timestamp, binding change, identity change, or excessive gap rejects the whole trace.
4. Use each acquisition's `scheduled_at` as its five-second window origin. It is eligible when `scheduled_at + 5 s <= exact_end`. Include every reading whose `read_started_at` is before that exact window end, and exclude a reading begun exactly at the end. Late scheduled acquisitions without five seconds remaining are not window starts. Do not omit an eligible start or choose a quieter subwindow.
5. Evaluate the eligible windows in one forward pass. Advance monotonic lower and upper read-start endpoints, and maintain monotonic minimum and maximum deques so each reading enters and leaves each deque at most once. Require at least 50 valid points in every eligible window. For each window, compute its checked minimum, maximum, and range `maximum - minimum`. Any invalid eligible window rejects the trace.
6. Across all eligible windows, let `L` be the greatest window minimum and `H` the least window maximum. Compute `checked_add(H, U)` and require `L <= checked_add(H, U)`. A false comparison or overflow rejects the whole trace.
7. Let `R` be the greatest window range. Compute `N_gtt = checked_add(R, U)`. Add `U` exactly once. Overflow rejects the trace.
8. Call `produce_no_target_gtt_noise(trace)`. On success, retain the returned `ParsedProfilingNoiseResult` and its canonical bytes as this component's output. Raw-trace and other evidence retention or persistence remain caller-owned; this step neither authorizes discarding them nor creates another store. The producer binds the validated trace content into `trace_provenance_sha256`, the exact identities, including the trace-era counter continuity epoch, into `bindings_sha256`, and the immutable result fields into `checksum_sha256`.

The implementation seam is `src/cpp/server/residency/profiling_noise.cpp`. Deterministic producer, boundary, trend, overflow, and provenance fixtures live in:

- `test/cpp/test_residency_profiling_noise.cpp`
- `test/cpp/test_residency_profiling_noise_trend.cpp`
- `test/cpp/test_residency_profiling_noise_provenance.cpp`

## Parse a retained result

1. Pass the exact retained bytes to `parse_profiling_noise_result(bytes)`.
2. Continue only when the parser returns `Accepted` with a result.
3. Preserve the returned canonical bytes and the exact bindings, including the trace-era counter continuity epoch, `N_gtt`, `U`, trace provenance, bindings digest, and result checksum.

Malformed JSON, duplicate or unknown fields, an unsupported schema, invalid identifiers or unsigned values, malformed digests, a digest mismatch, noncanonical bytes, excessive input, or unavailable hashing produces no parsed result. Corruption or a digest mismatch requires a fresh no-target trace; a `Fresh` calibration revision cannot rehabilitate the retained result. Canonical checksum validation proves content integrity under the implemented codec. It does not authenticate the observer or producer identity.

## Freeze differential input before target observation

1. Before starting the target, prepare one `ProfilingDifferentialInputDraft` under the Server-owned gate and existing journal.
2. Bind the exact transaction, selector, target client and containment identities, counter continuity epoch, safety contract, calibration revision, attempt receipt, and accounting partition. Set the transaction's observation-contract digest to the reviewed contract that covers once-only receipt sequencing and explicit complete, incomplete, or absent owner attribution; do not reuse a digest for the earlier raw-observation evaluator input. The draft epoch must equal the epoch bound into the immutable noise result.
3. Load the Server journal's current validity for the immutable noise-result checksum. Set `noise_validity.noise_result_checksum_sha256` to that exact checksum and set its state to `Valid` only when the journal has not invalidated it. A `Fresh` calibration revision does not make an invalidated noise result valid.
4. Copy `noise_trace_provenance_sha256` from the parsed immutable result. An arbitrary well-formed digest is not equivalent; `freeze_profiling_differential_input` requires exact equality.
5. Keep `N_gtt`, nonnegative disjoint `X_gtt`, nonnegative `M_gtt`, their evidence and policy digests, and the exact partition contract separate. Checked `N_gtt + X_gtt + M_gtt` must not overflow.
6. Set positive calibration and validation repetition counts.
7. Require a fresh calibration revision. A revision already rejected by the journal must not freeze again.
8. Call `freeze_profiling_differential_input(noise, draft)` before any target observation. Continue only when it returns `Accepted` with a `FrozenProfilingDifferentialInput`. Retain its `frozen_input_sha256`. A mismatched epoch, mismatched validity key, or `Invalidated` state fails closed.
9. Construct one `ProfilingDifferentialAttemptState` from that frozen input. The frozen input is immutable; the attempt state is the sole in-process owner of repetition order, the receipt-chain head, the last accepted observation time, terminal revision rejection, and monotone noise invalidation. Do not reuse the state with another frozen input.

This in-process freeze and attempt state do not authenticate an attempt receipt, attest the supplied validity state, or persist rejection, receipt progress, or invalidation. The Server caller must verify and durably record those values through the existing journal. Before every freeze, including after Server restart, it must reload the checksum-keyed noise validity and pass that state through `ProfilingNoiseValidityBinding`; it must not reconstruct an in-progress attempt from unauthenticated records. The implementation and deterministic fixtures are in `src/cpp/server/residency/profiling_differential_input.cpp` and `test/cpp/test_residency_profiling_differential_input.cpp`.

## Revalidate before every repetition

Immediately before every calibration or validation repetition, call `revalidate_profiling_differential_input(input, attempt, phase, ordinal, observation)` exactly once with the next frozen phase and ordinal and a fresh Server-authenticated observation. Recheck:

- the immutable noise-result checksum;
- deployment, boot, device, topology, kernel, driver, counter-source, campaign, procedure, and background bindings;
- the counter continuity epoch, with no reset or discontinuity;
- the exact non-target background inventory and absence of unexpected non-target clients;
- a non-target GTT range no greater than frozen `N_gtt`; and
- when target activity is expected, the exact frozen target client and containment identities.

Elapsed time alone does not expire a result within the same boot. Declared target activity inside its frozen containment is allowed. An unexpected non-target client, reboot, binding change, counter reset or discontinuity, checksum change, background drift, or variation above `N_gtt` rejects the revision and invalidates its noise result. A mismatch in the declared target or its containment rejects the revision without invalidating otherwise continuous noise. When one authenticated observation contains both a noise invalidator and a target mismatch or time-order defect, the invalidator wins.

Every in-order revalidation with a structurally valid observation returns an immutable `ProfilingDifferentialRevalidationReceipt` bound to the frozen-input checksum, noise-result checksum, phase, ordinal, observation digest, checked time, prior receipt digest, status, and disposition. Retain the receipt with that repetition. An accepted receipt with disposition `Continue` authorizes only that caller-owned repetition under the still-held Server gate and journal lease. Issue its baseline-ready marker immediately after revalidation without an intervening gate, lease, identity, counter, background, or journal-state change. The evaluator consumes the retained receipt and never replays the observation.

The attempt state accepts each frozen phase and ordinal once and in order. An out-of-order call, replay, malformed observation, or disposition other than `Continue` terminally rejects the attempt. A rejecting receipt authorizes no plateau bytes for its repetition, and no call or supplied record after terminal rejection has observation authority. Stop, produce no differential result, persist rejection through the existing journal, and do not refit the same revision.

`CounterReset`, `CounterDiscontinuity`, `NoiseResultMismatch`, `BindingMismatch`, `BackgroundDrift`, and `ExcessVariation` return disposition `InvalidateNoiseResult`. The Server caller must also persist invalidation keyed to the immutable noise-result checksum. It must reload that invalidation before a later freeze; only a newly accepted no-target result produced from a fresh trace is eligible as a replacement. A new calibration revision alone cannot revive the old result. `TargetMismatch` and `NonIncreasingObservation` return `RejectRevision` without invalidating otherwise continuous noise evidence. Authentication and both durable mutations remain Server-owned.

## Outcome branches

**Success:** production returns one canonical immutable noise result; parsing reproduces its identities, trace-era counter continuity epoch, bound, uncertainty, provenance, and checksum; freezing consumes a matching `Valid` checksum-keyed journal state and returns one immutable differential input before target observation; and one attempt state issues an ordered `Continue` receipt immediately before each repetition.

**Failure:** return or retain no result from the failing stage. Preserve the previous method or conservative fallback. Cleanup and release remain Server-owned. A revalidation failure terminally rejects the revision and cannot select a later window, restart within the same trace, or refit the same revision. A noise-invalidating disposition also prevents every later revision from consuming that result and requires a fresh no-target trace.

**No-op:** when the caller cannot prove authentic observations, target absence, exclusive gate ownership, exact journal state, or any required identity, do not produce or consume evidence. Do not replace missing proof with an empty digest, synthesized provenance, zero owner attribution, or elapsed-time assumptions.

## Completion evidence and limits

For this bounded component, observable completion evidence is:

- an accepted producer result whose canonical parser round-trips the same checksum, bindings, counter continuity epoch, trace provenance, `N_gtt`, and `U`, with both documented exact-cap clustered traces accepted by the linear window traversal;
- an accepted freeze whose epoch, provenance, and `Valid` checksum-keyed noise state match the producer result and whose digest covers the frozen accounting, identities, validity state, revision, and repetition counts;
- one ordered, once-only, checksum-bound `Continue` receipt issued immediately before each caller-owned repetition and consumed without replay;
- deterministic rejection of old-result/new-revision reuse after reset or excess background variation, acceptance of a newly produced result for the new epoch, and acceptance of uninterrupted same-boot reuse; and
- passing `ResidencyProfilingNoise`, `ResidencyProfilingNoiseTrend`, `ResidencyProfilingNoiseProvenance`, and `ResidencyProfilingDifferentialInput` CTests linked through `lemonade-server-core`.

These tests use deterministic fixtures. They do not qualify a live host or campaign.

The authoritative global GTT point and owner-projection completeness remain independent. Missing or incomplete fdinfo owner attribution never turns a valid global point into an owner zero. This point-based procedure also remains separate from the optional mutation-complete interval authority in `src/cpp/include/lemon/residency/profiling_capture_authority.h`; never advance an interval watermark from polls or observed value changes.

This component does not establish the Server gate, live collection, authenticated observation provenance, durable journal persistence or restart recovery, target plateau evaluation, retained differential calibration, verified release, actor continuity, the separate mutation-complete path, a complete transient lifecycle envelope, non-footprint claims, candidate persistence, activation, signing, publication, or release authority.

## Accepted policy

- [Noise-window and same-boot recommendations](https://github.com/nisavid/lemonade/issues/120#issuecomment-5693502638)
- [Accepted window, rejection, and boot-reuse policy](https://github.com/nisavid/lemonade/issues/120#issuecomment-5693599387)
- [Proposed common-level criterion, later accepted](https://github.com/nisavid/lemonade/issues/120#issuecomment-5694062797)
- [Accepted common-level trend criterion](https://github.com/nisavid/lemonade/issues/120#issuecomment-5694372812)
