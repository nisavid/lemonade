# Retained-GTT differential evaluator procedure

Use this procedure when an exclusive `lemond` profiling transaction has
frozen a boot-scoped differential input and collected complete baseline,
loaded, and release point records for an exact fingerprint. The procedure
produces one canonical retained-GTT component result. It does not collect
observations, authenticate records, persist journal decisions, complete a
transient lifecycle envelope, or authorize admission.

## Required upstream procedure

Load [the no-target GTT noise procedure](profiling-no-target-gtt-noise.md)
before freezing or evaluating input. This consumer was implemented against the
procedure merged in
[`17a1dc8721bd8a11e68ec957589d4d68c27dd027`](https://github.com/nisavid/lemonade/commit/17a1dc8721bd8a11e68ec957589d4d68c27dd027).
Its exact raw file bytes have SHA-256
`53154e34cf8387b0f7805accade0e431fec603470b48db5324d963b4b8b659ed`.
Do not substitute rendered Markdown, a moving branch, or a procedure from a
different implementation revision.

The upstream procedure supplies the immutable noise result, frozen input,
same-boot validity rules, and revalidation dispositions used here. The public
evaluator seam is declared in
`src/cpp/include/lemon/residency/profiling_differential_evaluator.h`; the
frozen-input seam remains in
`src/cpp/include/lemon/residency/profiling_differential_input.h`.

## Entry conditions and accepted records

The `lemond` caller must still hold the exclusive deployment profiling gate and
the journal lease for the frozen calibration revision. Before calling the
evaluator, it must authenticate and assemble:

1. One `FrozenProfilingDifferentialInput` with a fresh revision, a currently
   valid noise-result checksum, exact target and containment identities, and
   disjoint `N_gtt`, `X_gtt`, and `M_gtt` terms.
2. One exact `ProfilingDifferentialMethodBinding` returned by
   `resolve_retained_gtt_differential_method_binding` for method
   `differential_retained_gtt`, covered effect `retained_gtt`, and the
   transaction's retained byte-constraint instance.
3. Exactly the frozen calibration repetition count followed by exactly the
   frozen validation repetition count. Phase-local ordinals start at zero,
   and repetitions do not overlap.
4. For every repetition, a new authenticated
   `ProfilingDifferentialRevalidationObservation`, followed by `lemond`-issued
   baseline-ready, loaded-ready, and release-ready markers.
5. Complete ordered point records for each plateau. Every point carries its
   schedule, read start and finish, authoritative global GTT byte value,
   frozen-input and selector identities, target and containment identities,
   no-target/source bindings, provenance digest, and explicit owner-projection
   status. Absence is `Absent`, never a synthesized zero. Complete projection
   supplies a value no greater than the global point. Contradictory or shared
   GTT evidence is not an accepted record.

The component accepts values as supplied records. Only the `lemond` caller can
prove that markers and observations came from the live transaction, that the
gate excluded competing work, and that the attempt receipt and journal state
are authentic.

## Resolve revisions and preflight before freezing

The method revision is the lowercase SHA-256 of the exact raw bytes of this
file. CMake computes it with `file(SHA256 ...)` and compiles it into
`lemonade-server-core`; the resolver returns that compiled revision. The
review receipt for an invocation must name one immutable commit whose clean
review covers this procedure and its evaluator, and the `lemond` caller must
independently hash this file's raw bytes at that commit. Do not put that digest
inside this file, hash rendered Markdown, or treat a matching source digest as
runtime authentication.

Resolve `constraint_id` from the exact `gpu_shared_residency` byte-constraint
instance selected by `lemond` for the same transaction and later manifest.
`resolve_retained_gtt_differential_method_binding` derives
`constraint_revision_sha256` from these bytes, in order:

1. the ASCII domain `lemonade.residency.profiling-differential-constraint/v1`
   followed by one NUL byte; and
2. `constraint_id`, `gpu_shared_residency`, `bytes`, the canonical selector
   SHA-256, and the observation-contract SHA-256, each encoded as an unsigned
   64-bit big-endian byte length followed by its raw bytes.

Before calling `freeze_profiling_differential_input`, call
`preflight_retained_gtt_differential` with the parsed noise result, draft, and
resolved method binding. Continue only on `Accepted`. Preflight requires the
reviewed no-target procedure revision, the exact method and constraint
binding, positive calibration and validation counts, and at most 128 total
repetitions. Evaluation and parsing repeat the applicable checks, but that
terminal defense does not authorize collecting an input that preflight has
rejected. Constraint resolution, review-receipt verification, and record
authentication remain `lemond` obligations.

## Evaluate the fixed repetitions

Pass the frozen input by value to `evaluate_retained_gtt_differential`. A
moved-from `FrozenProfilingDifferentialInput` remains valid but has
unspecified state; do not inspect or reuse it as attempt authority. The
durable journal, not C++ move behavior, prevents another freeze of a rejected
revision.

Immediately before each repetition, the `lemond` caller authenticates a fresh
revalidation observation and then issues the baseline-ready marker without an
intervening gate, lease, identity, counter, background, or journal-state
change. This is an ordering and authority fence, not a numeric clock-expiry
rule. Elapsed time alone does not expire matching evidence during the same
boot. If the caller cannot preserve that fence, it obtains and authenticates a
new observation or stops the attempt.

For each repetition, the evaluator calls
`revalidate_profiling_differential_input`. It continues only for disposition
`Continue`, requires the accepted `checked_at` not to follow the baseline
marker, requires later repetitions to carry increasing observations, and
prevents overlap with completed release reads. Authentication of the
observation and the no-intervening-change fence remain `lemond` obligations.
The evaluator then applies the same rule independently to baseline, loaded,
and release:

1. Select the first scheduled acquisition on or after the matching marker.
2. Hold the exact five-second window that begins at that acquisition. Include
   every scheduled acquisition before the exact end and exclude an acquisition
   scheduled exactly at the end.
3. Require a strictly ordered schedule, at least 50 complete points, no
   schedule or read-start gap above 100 ms, unchanged frozen, actor,
   containment, and source bindings, and a global range no greater than the
   frozen `N_gtt`.
4. Reject the repetition rather than selecting a quieter later window.

After the three plateaus, the evaluator computes the nonnegative checked
delta

`D_r = max(loaded_r) - min(baseline_r)`.

The maximum calibration delta fixes

`B_retained_gtt = max(calibration D_r) + N_gtt + X_gtt + M_gtt`

with checked arithmetic. Validation equality is accepted; any larger
validation delta rejects the revision without changing the bound. For every
repetition, every recorded post-release point must remain in

`[max(0, min(baseline_r) - N_gtt), max(baseline_r) + N_gtt]`.

The next repetition cannot begin revalidation or baseline collection until
every checked post-release read from the preceding repetition has completed.

A missing or failed release marker, arithmetic failure, invalid point, drift,
unstable plateau, validation exceedance, or release-envelope breach produces
no component evidence.

## Consume the result without widening its claim

Success returns `ParsedProfilingDifferentialEvidence` and exact canonical
bytes. Parse retained bytes with `parse_profiling_differential_evidence` before
later composition. Preserve its checksum and these bounded facts:

- frozen-input checksum and exact selector fingerprint;
- immutable noise, target, containment, safety, and transaction identities;
- exact method, retained byte constraint, covered effect, and calibration
  revision;
- `N_gtt`, `X_gtt`, and `M_gtt`, their evidence or policy revisions, and the
  accounting-partition revision;
- ordered calibration and validation provenance digests and deltas;
- the checked retained byte claim and per-repetition release results; and
- the weakest owner-projection coverage observed, either `complete` or
  `absent`.

The component deliberately retains only repetition provenance digests and
fixed-window results, not complete transient history. It makes no claim about
external-demand absence, confidence class, admission-manifest completeness,
the transient lifecycle envelope, activation, signing, publication, or shared
catalog authority. Do not map absent fdinfo projection to zero or use this
component to complete another claim family.

## Rejection, invalidation, and persistence

Every failed evaluation rejects the calibration revision and returns no
evidence. The `lemond` caller must persist that rejection through the existing
journal and must not refit or retry the same revision.

When pre-repetition revalidation returns `InvalidateNoiseResult`, preserve that
disposition, persist invalidation under the immutable noise-result checksum,
and require a newly accepted no-target trace. A fresh calibration revision
cannot revive the invalidated result. `TargetMismatch`,
`NonIncreasingObservation`, plateau failures, validation exceedance, and
release failures reject only the revision unless an independent
noise-invalidating condition is also established.

A mismatch in any no-target source binding on a baseline, loaded, release, or
trailing checked release point is `SourceDrift` with disposition
`InvalidateNoiseResult`. The evaluation result carries the immutable
`noise_result_checksum_sha256` required for checksum-keyed persistence.
Identity, actor, marker, ordering, method, constraint, arithmetic, validation,
and release failures remain `RejectRevision` unless a separate
noise-invalidating condition is established. A parser rejection produces no
trusted component and does not by itself establish a new noise invalidation.
Authentication and both durable mutations remain `lemond`-owned.

## Outcome branches

**Success:** preflight accepts before freeze, every frozen repetition passes
fresh revalidation and fixed-window evaluation, and the canonical parser
round-trips the retained-only component checksum and bindings.

**Failure after freeze or collection begins:** stop the attempt, return no
component, persist the calibration-revision rejection, and do not refit it.
Also persist checksum-keyed noise invalidation when the returned disposition
is `InvalidateNoiseResult`. Cleanup and release remain `lemond`-owned.

**No-op before an attempt:** when `lemond` cannot prove the exclusive gate,
journal lease and validity state, reviewed revision receipt, authenticated
source, exact constraint resolution, or any required identity, do not freeze,
collect, or evaluate. Keep the prior method or conservative fallback and do
not create a rejection or invalidation merely because authority was absent.
Loss of authority after freeze is failure, not this no-op branch.

## Validation

Build with no more than four jobs and run the evaluator test plus the focused
profiling and local-overlay tests:

```bash
cmake --build --preset default --parallel 4 --target \
  test_residency_profiling_differential_evaluator \
  test_residency_profiling_differential_input \
  test_residency_profiling_noise \
  test_residency_profiling_noise_trend \
  test_residency_profiling_noise_provenance \
  test_residency_profiling_transaction \
  test_residency_profiling_provider \
  test_residency_profiling_interval \
  test_residency_profiling_capture_authority \
  test_residency_local_overlay
ctest --test-dir build --output-on-failure -R '^(ResidencyProfilingDifferentialEvaluator|ResidencyProfilingDifferentialInput|ResidencyProfilingNoise|ResidencyProfilingNoiseTrend|ResidencyProfilingNoiseProvenance|ResidencyProfilingTransaction|ResidencyProfilingProvider|ResidencyProfilingInterval|ResidencyProfilingCaptureAuthority|ResidencyLocalOverlay)$'
```

These tests use constructed deterministic records. They do not authenticate a
live source or qualify a host, driver, backend, model, workload, or release.
The optional mutation-complete interval remains the unchanged stronger path in
`src/cpp/include/lemon/residency/profiling_capture_authority.h`.

## Later `lemond` composition

Before this component can contribute to an admission candidate, later `lemond`
work must establish the exclusive gate and queued-client behavior, live point
collection and authentication, workload and containment ownership, actor
continuity, durable rejection and noise invalidation, verified cleanup, and
cancellation and restart handling. It must separately close the complete
transient lifecycle envelope, host floor, cardinality, ownership/recovery,
action-lease, and every other selector-required claim, then persist the whole
candidate atomically. Method selection, activation, qualification, signing,
catalog changes, and publication remain with their existing authorities.

The selected evidence boundary is recorded in
[issue 139](https://github.com/nisavid/lemonade/issues/139#issuecomment-5419853091),
and the retained differential contract remains in
[issue 120](https://github.com/nisavid/lemonade/issues/120).
