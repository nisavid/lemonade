# No-target GTT noise producer and consumer procedure

Use this procedure to create or consume one boot-scoped, immutable no-target `N_gtt` result through the public residency profiling interfaces. It validates trace structure and content bindings. It does not collect observations or authenticate who made them.

## Authority and entry conditions

The Server caller owns the exclusive deployment profiling gate, queued competing Lemonade work, target absence during production, process containment, authentic observation source, and durable journal state. Begin only when the caller has:

- excluded the declared target and other unaccounted GTT clients from the no-target trace;
- bound the lowercase 64-hex deployment identity and boot epoch, exact device and topology, kernel and driver, global counter source and revision, campaign contract, this reviewed procedure revision, and non-target background inventory;
- selected a measured read/skew uncertainty `U` in bytes;
- provided a deterministic 15-minute observation trace from the authoritative global `mem_info_gtt_used` counter; and
- established that the requested calibration revision is fresh in the existing Server journal.

The caller must bind `procedure_revision_sha256` to the reviewed revision of this procedure. The component accepts values and digests as input; it does not attest the observer, enforce the gate, inspect live hardware, or persist journal decisions.

The supported trace types and result interfaces are declared in `src/cpp/include/lemon/residency/profiling_noise.h`. The frozen consumer types are declared in `src/cpp/include/lemon/residency/profiling_differential_input.h`.

## Produce the immutable noise result

1. Build one `ProfilingNoTargetGttTrace` with an exact 15-minute `[started_at, exact_end)` boundary and the complete `ProfilingNoiseBindings`.
2. Schedule nominal 50 ms acquisitions, with no more than 36,000 readings in the trace. Every scheduled timestamp and read initiation must remain ordered inside the trace, the first schedule must equal `started_at`, and neither consecutive schedule nor consecutive read initiation may be more than 100 ms apart. The trace must reach its exact end without a gap above 100 ms.
3. For every acquisition, record `scheduled_at`, `read_started_at`, `read_finished_at`, a successful global GTT byte value, and the bindings rechecked for that acquisition. A failed read, reversed timestamp, binding change, identity change, or excessive gap rejects the whole trace.
4. Use each acquisition's `scheduled_at` as its five-second window origin. It is eligible when `scheduled_at + 5 s <= exact_end`. Include every reading whose `read_started_at` is before that exact window end, and exclude a reading begun exactly at the end. Late scheduled acquisitions without five seconds remaining are not window starts. Do not omit an eligible start or choose a quieter subwindow.
5. Evaluate the eligible windows in one forward pass. Advance monotonic lower and upper read-start endpoints, and maintain monotonic minimum and maximum deques so each reading enters and leaves each deque at most once. Require at least 50 valid points in every eligible window. For each window, compute its checked minimum, maximum, and range `maximum - minimum`. Any invalid eligible window rejects the trace.
6. Across all eligible windows, let `L` be the greatest window minimum and `H` the least window maximum. Compute `checked_add(H, U)` and require `L <= checked_add(H, U)`. A false comparison or overflow rejects the whole trace.
7. Let `R` be the greatest window range. Compute `N_gtt = checked_add(R, U)`. Add `U` exactly once. Overflow rejects the trace.
8. Call `produce_no_target_gtt_noise(trace)`. On success, retain the returned `ParsedProfilingNoiseResult` and its canonical bytes as this component's output. Raw-trace and other evidence retention or persistence remain caller-owned; this step neither authorizes discarding them nor creates another store. The producer binds the validated trace content into `trace_provenance_sha256`, the exact identities into `bindings_sha256`, and the immutable result fields into `checksum_sha256`.

The implementation seam is `src/cpp/server/residency/profiling_noise.cpp`. Deterministic producer, boundary, trend, overflow, and provenance fixtures live in:

- `test/cpp/test_residency_profiling_noise.cpp`
- `test/cpp/test_residency_profiling_noise_trend.cpp`
- `test/cpp/test_residency_profiling_noise_provenance.cpp`

## Parse a retained result

1. Pass the exact retained bytes to `parse_profiling_noise_result(bytes)`.
2. Continue only when the parser returns `Accepted` with a result.
3. Preserve the returned canonical bytes and the exact bindings, `N_gtt`, `U`, trace provenance, bindings digest, and result checksum.

Malformed JSON, duplicate or unknown fields, an unsupported schema, invalid identifiers or unsigned values, malformed digests, a digest mismatch, noncanonical bytes, excessive input, or unavailable hashing produces no parsed result. Canonical checksum validation proves content integrity under the implemented codec. It does not authenticate the observer or producer identity.

## Freeze differential input before target observation

1. Before starting the target, prepare one `ProfilingDifferentialInputDraft` under the Server-owned gate and existing journal.
2. Bind the exact transaction, selector, target client and containment identities, counter continuity epoch, safety contract, calibration revision, attempt receipt, and accounting partition.
3. Copy `noise_trace_provenance_sha256` from the parsed immutable result. An arbitrary well-formed digest is not equivalent; `freeze_profiling_differential_input` requires exact equality.
4. Keep `N_gtt`, nonnegative disjoint `X_gtt`, nonnegative `M_gtt`, their evidence and policy digests, and the exact partition contract separate. Checked `N_gtt + X_gtt + M_gtt` must not overflow.
5. Set positive calibration and validation repetition counts.
6. Require a fresh calibration revision. A revision already rejected by the journal must not freeze again.
7. Call `freeze_profiling_differential_input(noise, draft)` before any target observation. Continue only when it returns `Accepted` with a `FrozenProfilingDifferentialInput`. Retain its `frozen_input_sha256`.

This in-process freeze does not authenticate an attempt receipt or persist rejection. The Server caller must verify those values against the existing durable journal and must record terminal rejection there. The implementation and deterministic fixtures are in `src/cpp/server/residency/profiling_differential_input.cpp` and `test/cpp/test_residency_profiling_differential_input.cpp`.

## Revalidate before every repetition

Immediately before every calibration or validation repetition, call `revalidate_profiling_differential_input(input, observation)` with a fresh Server-authenticated observation. Recheck:

- the immutable noise-result checksum;
- deployment, boot, device, topology, kernel, driver, counter-source, campaign, procedure, and background bindings;
- the counter continuity epoch, with no reset or discontinuity;
- the exact non-target background inventory and absence of unexpected non-target clients;
- a non-target GTT range no greater than frozen `N_gtt`; and
- when target activity is expected, the exact frozen target client and containment identities.

Elapsed time alone does not expire a result within the same boot. Declared target activity inside its frozen containment is allowed. Another client, containment mismatch, reboot, binding change, counter reset or discontinuity, checksum change, background drift, or variation above `N_gtt` rejects the revision.

An accepted revalidation authorizes only the caller's next repetition under the still-held Server gate and journal lease. Any rejection is terminal for that calibration revision: stop, produce no differential result, persist rejection through the existing journal, and do not refit the same revision.

## Outcome branches

**Success:** production returns one canonical immutable noise result; parsing reproduces its identities, bound, uncertainty, provenance, and checksum; freezing returns one immutable differential input before target observation; and every repetition begins with an accepted revalidation.

**Failure:** return or retain no result from the failing stage. Preserve the previous method or conservative fallback. Cleanup and release remain Server-owned. A revalidation failure terminally rejects the revision and cannot select a later window, restart within the same trace, or refit the same revision.

**No-op:** when the caller cannot prove authentic observations, target absence, exclusive gate ownership, exact journal state, or any required identity, do not produce or consume evidence. Do not replace missing proof with an empty digest, synthesized provenance, zero owner attribution, or elapsed-time assumptions.

## Completion evidence and limits

For this bounded component, observable completion evidence is:

- an accepted producer result whose canonical parser round-trips the same checksum, bindings, trace provenance, `N_gtt`, and `U`, with both documented exact-cap clustered traces accepted by the linear window traversal;
- an accepted freeze whose provenance equals the producer result and whose digest covers the frozen accounting, identities, revision, and repetition counts;
- an accepted fresh revalidation before each caller-owned repetition; and
- passing `ResidencyProfilingNoise`, `ResidencyProfilingNoiseTrend`, `ResidencyProfilingNoiseProvenance`, and `ResidencyProfilingDifferentialInput` CTests linked through `lemonade-server-core`.

These tests use deterministic fixtures. They do not qualify a live host or campaign.

The authoritative global GTT point and owner-projection completeness remain independent. Missing or incomplete fdinfo owner attribution never turns a valid global point into an owner zero. This point-based procedure also remains separate from the optional mutation-complete interval authority in `src/cpp/include/lemon/residency/profiling_capture_authority.h`; never advance an interval watermark from polls or observed value changes.

This component does not establish the Server gate, live collection, authenticated observation provenance, durable journal persistence, target plateau evaluation, retained differential calibration, verified release, actor continuity, the separate mutation-complete path, a complete transient lifecycle envelope, non-footprint claims, candidate persistence, activation, signing, publication, or release authority.

## Accepted policy

- [Noise-window and same-boot recommendations](https://github.com/nisavid/lemonade/issues/120#issuecomment-5693502638)
- [Accepted window, rejection, and boot-reuse policy](https://github.com/nisavid/lemonade/issues/120#issuecomment-5693599387)
- [Proposed common-level criterion, later accepted](https://github.com/nisavid/lemonade/issues/120#issuecomment-5694062797)
- [Accepted common-level trend criterion](https://github.com/nisavid/lemonade/issues/120#issuecomment-5694372812)
