# Retained-GTT differential evaluator procedure

Use this procedure when an exclusive `lemond` profiling transaction has
frozen a boot-scoped differential input and collected complete baseline,
loaded, and release point records for an exact fingerprint. The procedure
produces one canonical retained-GTT component result. It does not collect
observations, authenticate records, persist journal decisions, complete a
transient lifecycle envelope, or authorize admission.

## Required upstream procedure

Load [the no-target GTT noise procedure](profiling-no-target-gtt-noise.md)
before freezing or evaluating input. This consumer requires producer procedure
bytes with raw-file SHA-256
`5a7a73e909d08133ee6d8b5d3f4e27535f95107d4f04164801b6f6d14ec4c292`.
The invocation review receipt must bind those bytes and the corresponding
implementation at one immutable revision. Do not substitute rendered Markdown,
a moving branch, or a procedure from a different implementation revision.

`.gitattributes` declares both this consumer and the upstream producer as
`text eol=lf`. A conversion-enabled checkout must therefore reproduce the
reviewed producer digest and preserve this consumer's reviewed raw bytes.
CMake hashes those checkout bytes without normalization: the LF contract
makes the binding portable, while any content change still changes the
revision or fails the fixed producer check.

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
   disjoint `N_gtt`, `X_gtt`, and `M_gtt` terms. Its frozen draft contains the
   complete method binding used for this attempt.
2. That same exact `ProfilingDifferentialMethodBinding`, returned by
   `resolve_retained_gtt_differential_method_binding` for method
   `differential_retained_gtt`, covered effect `retained_gtt`, and the
   transaction's retained byte-constraint instance.
3. Exactly the frozen calibration repetition count followed by exactly the
   frozen validation repetition count. Phase-local ordinals start at zero,
   and repetitions do not overlap.
4. One `lemond`-owned `ProfilingDifferentialAttemptState` and, for every
   repetition, an immutable once-only
   `ProfilingDifferentialRevalidationReceipt` issued from a newly authenticated
   observation for that exact phase and ordinal, followed without an
   intervening state change by `lemond`-issued baseline-ready, loaded-ready,
   and release-ready markers.
5. Complete ordered point records for each plateau. Every point carries its
   schedule, read start and finish, authoritative global GTT byte value,
   frozen-input and selector identities, target and containment identities,
   no-target/source bindings, provenance digest, and explicit owner-projection
   status. Absence is `Absent` with no owner value, never a synthesized zero.
   `Incomplete` may carry no value or a partial value no greater than the
   global point, but it supplies no capacity authority. `Complete` supplies a
   value no greater than the global point. Contradictory or shared GTT evidence
   is not an accepted record.
6. Bounded plateau collection retains no more than 4096 point records. Every
   retained digest is exactly 64 lowercase hexadecimal bytes, and every
   retained identifier is printable and at most 128 bytes. When a
   4097th record arrives, authenticate it under the same caller-owned source
   boundary, expose it for the evaluator's constant-size source-fact audit,
   latch overflow, and stop collection. Do not collect or represent a tail as
   usable evidence.

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
The transaction's observation-contract digest must name the reviewed contract
for once-only revalidation receipts, their no-intervening-change fence, and
explicit complete, incomplete, or absent owner attribution. The earlier
raw-observation evaluator contract is not compatible. Changing this digest
changes the derived constraint revision; do not relabel evidence produced under
the earlier contract.

`resolve_retained_gtt_differential_method_binding` derives
`constraint_revision_sha256` from these bytes, in order:

1. the ASCII domain `lemonade.residency.profiling-differential-constraint/v1`
   followed by one NUL byte; and
2. `constraint_id`, `gpu_shared_residency`, `bytes`, the canonical selector
   SHA-256, and the observation-contract SHA-256, each encoded as an unsigned
   64-bit big-endian byte length followed by its raw bytes.

Store the resolved binding in `draft.method_binding`, then call
`preflight_retained_gtt_differential` with the parsed noise result, that draft,
and the same binding. Continue only on `Accepted`, and freeze that unchanged
draft before collecting any target observation. The version-2 frozen-input
digest covers every method-binding field, and each revalidation receipt binds
that frozen-input digest into its receipt chain. Preflight requires the
reviewed no-target procedure revision, exact agreement between the draft and
supplied method and constraint binding, positive calibration and validation
counts, and at most 128 total repetitions. Evaluation requires the supplied
binding to equal the frozen binding and independently resolves the supported
binding again. A separately resolved, internally consistent binding cannot
relabel records collected under the frozen binding. Parsing repeats the
applicable support checks, but those terminal defenses do not authorize
collecting an input that preflight has rejected. Constraint resolution,
review-receipt verification, and record authentication remain `lemond`
obligations.

## Evaluate the fixed repetitions

Pass the immutable frozen input, the same method binding used for preflight,
and the caller-owned repetition vector to
`evaluate_retained_gtt_differential`. The evaluator does not
mutate the input, advance the attempt state, replay revalidation, or copy an
untrusted vector. Its bounded ingestion borrows only the first 4096 records
from each plateau, audits the 4097th record as described below, latches
overflow, and does not inspect a later vector tail. Overflow always rejects
evidence, so ignored tail bytes never make the plateau safe. The durable
journal, not C++ object lifetime, prevents another freeze of a rejected
revision.

Immediately before each repetition, the `lemond` caller advances the attempt
state once with a fresh authenticated observation, retains the returned
receipt with that repetition, and then issues the baseline-ready marker without
an intervening gate, lease, identity, counter, background, or journal-state
change. This is an ordering and authority fence, not a numeric clock-expiry
rule. Elapsed time alone does not expire matching evidence during the same
boot. Receipt issuance consumes that repetition's phase and ordinal. If the
caller loses the fence after receipt issuance, it stops the attempt and follows
the rejection and cleanup path below.

### Establish the strongest observation disposition

Method, procedure, and bounded repetition-count checks first establish that
the supplied records belong to this evaluator attempt. The evaluator then
validates the receipt chain from the frozen attempt-receipt digest through the
exact calibration and validation phase ordinals. It stops at the first
non-`Continue` receipt. An invalidating receipt wins globally before ordinary
classification. A `Continue` receipt authorizes only its own point audit; a
terminal `TargetMismatch`, another revision-only rejection, a missing receipt,
or an invalid chain authorizes neither that repetition's plateau bytes nor any
later record. When one authenticated observation contains both a target defect
and a reset or drift, the receipt records the invalidating condition.

For every `Continue`-authorized repetition, the evaluator ingests each plateau
into two independent results: bounded evidence admissibility and monotone noise
validity. It borrows at most the first 4096 records, derives only the scheduled
time, structural usability, and binding comparison from the 4097th record,
latches overflow, and stops. It audits every authorized repetition before any
repetition-order, window, identity, actor, timing, projection, arithmetic,
validation, or serialization failure can return. An expected-kind ready marker
with structurally valid digest fields defines the source-audit time scope even
when a separate marker identity check will reject it. A point establishes a
source fact only when it has an authoritative global value, coherent
scheduled/read times, a valid provenance digest, and structurally valid
no-target binding fields. Empty plateaus, unusable markers, incomplete points,
and malformed binding bytes do not establish source drift. Plateau overflow is
an evidence defect, but it cannot erase source drift established by a retained
record or the 4097th record.

The audit covers the selected five-second baseline and loaded scopes and every
ingested release fact scheduled at or after the release marker, including a
trailing 4097th record. It completes across those eligible scopes before
ordinary classification. Any mismatch with the immutable no-target bindings
is therefore the strongest result: `SourceDrift` with
`InvalidateNoiseResult` and the immutable noise-result checksum, even when an
independent revision-only defect is also present or appears earlier. Without
an established mismatch, overflow is `InvalidWindow` with `RejectRevision`,
no checksum, and no evidence. A vector tail after the audited 4097th record is
never scanned and cannot establish a fact; the latched overflow still rejects
the revision. Receipt production applies the same severity ordering to its
authenticated fields. For every authorized retained record, the evaluator
checks all digest and identifier byte bounds before provenance hashing,
including records outside the selected fixed window. It then streams the
length-prefixed receipt, marker, binding, and point fields directly into a
bounded SHA-256 context and checks the combined encoded-byte total across at
most 128 repetitions. Terminal hashing and serialization run only after all
authorized repetitions have passed the global audit, so they cannot downgrade
an observed invalidator.

For each accepted receipt, the evaluator requires `checked_at` not to follow
the baseline marker, requires later repetitions to carry increasing receipt
times, and prevents overlap with completed release reads. It does not call
`revalidate_profiling_differential_input`. Authentication of the source
observation and the no-intervening-change fence remain `lemond` obligations.
The evaluator then applies the same rule independently to baseline, loaded,
and release:

1. Select the first scheduled acquisition on or after the matching marker.
2. Hold the exact five-second window that begins at that acquisition. Include
   every scheduled acquisition before the exact end and exclude an acquisition
   scheduled exactly at the end.
3. Before collection, the `lemond` caller configures a 50 ms nominal
   acquisition cadence. The evaluator requires a strictly ordered schedule,
   at least 50 complete points, no schedule or read-start gap above 100 ms,
   unchanged frozen, actor, containment, and source bindings, and a global
   range no greater than the frozen `N_gtt`. Exactly 4096 records remains
   admissible when every other check passes. Record 4097 always latches
   overflow and makes the plateau inadmissible, independently of the monotone
   source-fact result. The 100 ms observed-gap limit is tolerance, not an
   alternate nominal cadence; evaluator acceptance alone does not prove the
   caller's scheduler configuration.
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
bytes only after the evaluator has parsed its own serialization and reproduced
those bytes exactly. The shared component ceiling is 66,565 bytes: 24,709
bytes for the maximum canonical envelope and array syntax plus 327 bytes for
each of at most 128 repetition records. The bound covers 128-byte printable
identifiers with JSON escaping, 32 material profiles, all nine constraints,
maximum-width unsigned values, fixed digests, and the longest closed wire
values. `parse_profiling_differential_evidence` rejects one byte above this
component limit before JSON parsing. Parse retained bytes before later
composition and preserve its checksum and these bounded facts:

- frozen-input checksum and exact selector fingerprint;
- immutable noise, target, containment, safety, and transaction identities;
- exact method, retained byte constraint, covered effect, and calibration
  revision;
- `N_gtt`, `X_gtt`, and `M_gtt`, their evidence or policy revisions, and the
  accounting-partition revision;
- ordered calibration and validation provenance digests and deltas;
- the checked retained byte claim and per-repetition release results; and
- the weakest owner-projection coverage observed: `complete`, `incomplete`, or
  `absent`.

The component deliberately retains only repetition provenance digests and
fixed-window results, not complete transient history. `Absent` carries no
owner amount. `Incomplete` may report partial point amounts during evaluation,
but neither it nor `Absent` supplies owner capacity authority. The canonical
component retains only the weakest coverage state and the authoritative global
bound. It makes no claim about
external-demand absence, confidence class, admission-manifest completeness,
the transient lifecycle envelope, activation, signing, publication, or shared
catalog authority. Do not map absent fdinfo projection to zero or use this
component to complete another claim family.

## Rejection, invalidation, and persistence

Every failed evaluation rejects the calibration revision and returns no
evidence. The `lemond` caller must persist that rejection through the existing
journal and must not refit or retry the same revision.

When a retained revalidation receipt has `InvalidateNoiseResult`, preserve
that disposition, persist invalidation under the immutable noise-result
checksum, and require a newly accepted no-target trace. An earlier negative
delta, marker, overflow, projection, ordering, arithmetic, validation, or
release failure cannot mask a later authoritative invalidating receipt or a
later `Continue`-authorized source drift. A fresh calibration revision cannot
revive the invalidated result. `TargetMismatch`,
`NonIncreasingObservation`, plateau failures, validation exceedance, and
release failures reject only the revision unless an independent
noise-invalidating condition is also established.

An established mismatch in any no-target source binding on a selected
baseline or loaded point, or any ingested release point, is `SourceDrift` with
disposition `InvalidateNoiseResult`. This includes a mismatch among the first
4096 records or in the bounded 4097th-record fact even though the plateau is
also oversized. The evaluation result carries the immutable
`noise_result_checksum_sha256` required for checksum-keyed persistence.
Identity, actor, marker, ordering, point-count, method, constraint, arithmetic,
validation, and release failures remain `RejectRevision` only when the
source-fact audit establishes no separate noise invalidator. A parser rejection
produces no trusted component and does not by itself establish a new noise
invalidation. Authentication and both durable mutations remain
`lemond`-owned.

## Outcome branches

**Success:** preflight accepts before freeze, every frozen repetition carries
an ordered `Continue` receipt and passes fixed-window evaluation, and the
canonical parser round-trips the retained-only component checksum and
bindings.

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
  test_residency_profiling_differential_hash_failures \
  test_residency_profiling_noise \
  test_residency_profiling_noise_trend \
  test_residency_profiling_noise_provenance \
  test_residency_profiling_transaction \
  test_residency_profiling_provider \
  test_residency_profiling_interval \
  test_residency_profiling_capture_authority \
  test_residency_local_overlay
ctest --test-dir build --output-on-failure -R '^(ResidencyProfilingDifferentialEvaluator|ResidencyProfilingDifferentialInput|ResidencyProfilingDifferentialHashFailures|ResidencyProfilingNoise|ResidencyProfilingNoiseTrend|ResidencyProfilingNoiseProvenance|ResidencyProfilingTransaction|ResidencyProfilingProvider|ResidencyProfilingInterval|ResidencyProfilingCaptureAuthority|ResidencyLocalOverlay)$'
```

These tests use constructed deterministic records. They do not authenticate a
runtime observer. They exercise the Linux evaluator contract locally; hosted
Windows and macOS checks, live device behavior, and physical campaign
qualification remain separate evidence.
The optional mutation-complete interval remains the unchanged stronger path in
`src/cpp/include/lemon/residency/profiling_capture_authority.h`.

## Later `lemond` composition

Before this component can contribute to an admission candidate, later `lemond`
work must load and invoke this maintained procedure and its required no-target
procedure from one reviewed source revision. It must establish the exclusive
gate and queued-client behavior, own and advance one attempt state, retain each
receipt before issuing the corresponding
marker, perform live point collection and authentication, preserve workload
and containment ownership and actor continuity, durably journal rejection and
noise invalidation, verify cleanup, and handle cancellation and restart. It
must supply the revised observation-contract digest and freshly produced noise
evidence bound to the required producer procedure; old immutable noise evidence
is not relabeled. It must separately close the complete
transient lifecycle envelope, host floor, cardinality, ownership/recovery,
action-lease, and every other selector-required claim, then persist the whole
candidate atomically. Method selection, activation, qualification, signing,
catalog changes, and publication remain with their existing authorities.

The selected evidence boundary is recorded in
[issue 139](https://github.com/nisavid/lemonade/issues/139#issuecomment-5419853091),
and the retained differential contract remains in
[issue 120](https://github.com/nisavid/lemonade/issues/120).
