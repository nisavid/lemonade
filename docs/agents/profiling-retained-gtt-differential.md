# Retained-GTT differential evaluator procedure

Use this procedure when an exclusive Server profiling transaction has frozen a
boot-scoped differential input and collected complete baseline, loaded, and
release point records for an exact fingerprint. The procedure produces one
canonical retained-GTT component result. It does not collect observations,
authenticate records, persist journal decisions, complete a transient
lifecycle envelope, or authorize admission.

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

The Server caller must still hold the exclusive deployment profiling gate and
the journal lease for the frozen calibration revision. Before calling the
evaluator, it must authenticate and assemble:

1. One `FrozenProfilingDifferentialInput` with a fresh revision, a currently
   valid noise-result checksum, exact target and containment identities, and
   disjoint `N_gtt`, `X_gtt`, and `M_gtt` terms.
2. One exact `ProfilingDifferentialMethodBinding` for method
   `differential_retained_gtt`, covered effect `retained_gtt`, and the bound
   byte constraint and method revisions.
3. Exactly the frozen calibration repetition count followed by exactly the
   frozen validation repetition count. Phase-local ordinals start at zero,
   and repetitions do not overlap.
4. For every repetition, a new authenticated
   `ProfilingDifferentialRevalidationObservation`, followed by Server-issued
   baseline-ready, loaded-ready, and release-ready markers.
5. Complete ordered point records for each plateau. Every point carries its
   schedule, read start and finish, authoritative global GTT byte value,
   frozen-input and selector identities, target and containment identities,
   no-target/source bindings, provenance digest, and explicit owner-projection
   status. Absence is `Absent`, never a synthesized zero. Complete projection
   supplies a value no greater than the global point. Contradictory or shared
   GTT evidence is not an accepted record.

The component accepts values as supplied records. Only the Server caller can
prove that markers and observations came from the live transaction, that the
gate excluded competing work, and that the attempt receipt and journal state
are authentic.

## Evaluate the fixed repetitions

Call `evaluate_retained_gtt_differential` once, moving the frozen input into
the call. The move makes the in-process attempt one-shot; the durable journal
remains the authority that prevents another freeze of a rejected revision.

For each repetition, the evaluator first calls
`revalidate_profiling_differential_input`. It continues only for disposition
`Continue`. It then applies the same rule independently to baseline, loaded,
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
evidence. The Server caller must persist that rejection through the existing
journal and must not refit or retry the same revision.

When pre-repetition revalidation returns `InvalidateNoiseResult`, preserve that
disposition, persist invalidation under the immutable noise-result checksum,
and require a newly accepted no-target trace. A fresh calibration revision
cannot revive the invalidated result. `TargetMismatch`,
`NonIncreasingObservation`, plateau failures, validation exceedance, and
release failures reject only the revision unless an independent
noise-invalidating condition is also established. Authentication and both
durable mutations remain Server-owned.

## Validation

Build with no more than four jobs and run the evaluator test plus the focused
profiling and local-overlay tests:

```bash
cmake --build build --parallel 4 --target \
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

## Later Server composition

Before this component can contribute to an admission candidate, later Server
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
