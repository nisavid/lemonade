---
goal: Implement portable, recovery-safe model residency and qualify Hatchery
version: 1.0
date_created: 2026-08-14
last_updated: 2026-08-14
owner: Ivan D Vasin
status: Planned
tags: [architecture, residency, recovery, validation]
---

# Introduction

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

This plan implements the accepted portable model-residency contract in dependency order. The source-closed upstream research baseline is `a505bbc702cc1fcd44ef73c44defabc98c36d505`; it classifies support but is not the runtime implementation base. Before runtime changes, the branch must reconcile the latest verified stable upstream release, record that implementation-base commit separately, and re-scout every changed seam. Every promotion unit stays at its cataloged fallback while implementation or evidence is incomplete, and the plan ends with independent physical qualification of the four Hatchery llama.cpp/ROCm operation cells plus separate synthetic qualification of the XDNA2 relation contract. The implementation must not infer policy from runtime observations, transfer evidence between cells, or grant live authority from partial code.

## 1. Requirements & Constraints

- **REQ-001**: Preserve `evidence_ceiling`, accepted `capability_level`, `delivery_state`, and live effective authority as four independent state dimensions. Compute effective mode as their conservative meet.
- **REQ-002**: Treat `docs/research/portable-residency-capability-inventory.json` as the source-closed machine contract. A changed upstream support atom, profile identity, backend artifact closure, operation, fallback, or gate set requires a new validated inventory revision.
- **REQ-003**: Resolve every residency operation through one server-owned catalog, planner, claim ledger, lifecycle coordinator, and explanation registry. Backend adapters provide facts and bounded mechanisms but never select policy.
- **REQ-004**: Prepare a complete manifest for every possible lifetime effect before reservation, victim selection, backend spawn, destructive dispatch, or residency commit. Unknown demand or incomplete identity must select the cataloged refusal or non-mutating fallback.
- **REQ-005**: Publish every authority-changing transition through one durable journal root. Recovery must replay claims before lifecycle readiness, reject concurrent daemon ownership, quarantine ambiguous effects, and release claims only after verified physical and ledger reconciliation.
- **REQ-006**: Protect pinned, in-use, leased, provisional, committing, rolling-back, cleanup-required, and quarantined residents according to the accepted operation contract. No NPU conflict, pressure, reconfiguration, retry, startup, or artifact path may bypass protection.
- **REQ-007**: Keep count limiting independent from predictive memory admission. `max_loaded_models` remains a per-model-type committed-resident ceiling and does not make unknown memory demand safe. Pinned and provisional residents count in their own type bucket.
- **REQ-008**: Generate operation phases, reason identifiers, fallbacks, schemas, HTTP mappings, compatibility projections, conformance fixtures, and packaged catalog records from one versioned registry.
- **REQ-009**: Implement canonical configuration for `memory_pressure_reclamation`, `load_pinned_models_on_startup`, nullable `residency_memory_limit_gib`, and the existing independent `max_loaded_models` ceiling through conditional, journaled authority.
- **REQ-010**: Keep saved pin preference, runtime pin state, startup-loading policy, and current residency distinct through configuration, migration, API, journal, and explanation paths.
- **REQ-011**: Package the accepted catalog with release artifacts and keep primary physical evidence as separately addressable immutable assets. Runtime observations remain local and cannot self-promote a catalog entry.
- **REQ-012**: Bind continuing destructive or capacity authority to a fresh evidence-liveness lease and anti-rollback head. Expiry, rollback, fork, witness failure, or evidence unavailability must immediately select the registered fallback without rewriting catalog history.
- **REQ-013**: Verify `delivery_state=release_verified` against the exact packaged executable, dependency closure, catalog, generated resources, and configuration defaults before any physical promotion campaign.
- **REQ-014**: Promote the Hatchery ADM, PRE, STA, and REC cells independently. A valid bound exceedance or invariant failure permanently blocks the implicated predictor-rule revision until a new immutable revision and disjoint held-out campaign succeed.
- **REQ-015**: Route artifact acquisition, registration-capable pulls, staged import/adoption, model deletion, cleanup, and shutdown through the same ordered configuration, identity, artifact-writer, recipe-options, and lifecycle authorities as model loads. Ambiguity transfers complete plausible claims and fences into recovery authority; it never reports release.
- **REQ-016**: Cut over each operation through one durable authority generation. The fork's `plan_gpu_memory_admission` path, stable-upstream `EvictionEngine`/`GlobalVramMonitor`, direct router eviction/retry, watchdog termination, and the new coordinator must never make automatic decisions concurrently.
- **REQ-017**: Advance delivery only through `absent` → inactive reviewed `implemented_unverified` candidate → exact-package `release_verified`. Physical or synthetic evidence changes capability in a later catalog revision and never substitutes for delivery verification.
- **REQ-018**: Persist content-addressed recovery-origin evidence before a journal record may reference it. Reachability, tombstone, deleting, revival, garbage collection, missing-object corruption, and topology/catalog rebind are transactional authority states rather than filesystem conventions.
- **SEC-001**: Apply existing API-key authorization to every new public residency route and admin authorization plus loopback restriction to every new internal mutation or recovery route.
- **SEC-002**: Treat journal, catalog, evidence, witness, and migration inputs as untrusted persisted data. Reject unknown major versions, duplicate keys, truncation, invalid signatures, path traversal, identity mismatch, and rollback before granting authority.
- **SEC-003**: Never persist credentials in the catalog, evidence bundle, journal, logs, or generated explanation records. Store witness keys and provider credentials through existing secret mechanisms.
- **CON-001**: Preserve the subprocess backend model. Residency code may prepare, observe, contain, and terminate backend processes but must not move inference in-process.
- **CON-002**: Register every new core HTTP endpoint under `/api/v0/`, `/api/v1/`, `/v0/`, and `/v1/`; preserve Ollama and Anthropic compatibility behavior.
- **CON-003**: Keep the server authoritative. Desktop, web, tray, and CLI clients render server state and must not reconstruct residency policy or persist server-owned configuration locally.
- **CON-004**: Maintain Windows, Linux, and macOS compilation. Platform observation, locking, process identity, and verified-release code must use explicit adapters and platform guards.
- **CON-005**: Partial implementation never changes a promotion unit from `delivery_state=absent`. Only a complete unit in a reviewed candidate catalog may become `implemented_unverified`.
- **CON-006**: Synthetic or analytic evidence cannot authorize Hatchery capacity credit or automatic hard reclamation. Only the exact physical gate set may advance an applicable operation cell to `validated`.
- **CON-007**: Create later-platform implementation and qualification tickets immediately after the refreshed support roster is frozen, then leave them fallback-only until their exact prerequisites are met. Do not split atomic gate IDs into separate deliverables.
- **GUD-001**: Use TDD at public seams: generated resources, journal recovery, provider contracts, planner outputs, lifecycle outcomes, API responses, package contents, and physical evidence manifests.
- **GUD-002**: Use deterministic named barriers and injected faults instead of timing sleeps for concurrency and crash tests. Retain the seed and full failure bundle for every failed attempt.
- **GUD-003**: Land each phase as a reviewable, green checkpoint. Keep dispatch disabled or fallback-only until all dependencies and phase gates are complete.
- **PAT-001**: Use a generated-registry pattern: JSON source contract to deterministic C++ types, packaged resources, schemas, reason tables, and conformance fixtures with byte-drift checks.
- **PAT-002**: Use a prepare-reserve-dispatch-reconcile-commit transaction pattern for every effectful operation. The journal publication point is the sole authority boundary.
- **PAT-003**: Use provider adapters with typed envelopes for topology, observations, effect attribution, process ownership, physical release, and evidence health. Represent unknown as a first-class result, never zero.
- **PAT-004**: Use generation-bound immutable plan tokens. Revalidate catalog, configuration, topology, observation, claims, protection, evidence-liveness, and recovery generations at every irreversible edge.

### Accepted public configuration

| Key | Accepted domain | Default | Contract |
|---|---|---|---|
| `memory_pressure_reclamation` | `automatic\|report_only\|disabled` | `disabled` | Operator intent only. Missing capability or evidence selects the operation fallback and never upgrades authority. |
| `load_pinned_models_on_startup` | Boolean | `false` | Controls grouped startup only; it does not alter saved pins, runtime pins, or existing residents. |
| `residency_memory_limit_gib` | `null` or positive number | `null` | Independently tightens complete claim exposure in each canonical GPU domain. |
| `max_loaded_models` | `-1` or positive integer | `1` | Independent ceiling for each model-type bucket; pinned and provisional residents count in their bucket and `-1` disables the ceiling. Lowering below a bucket's occupancy fences count-increasing work in that bucket but never evicts by count alone. |

Predictive capacity admission is fixed target policy. Sensor selection, GTT toggles, thresholds, dwell, polling, calibration, victim weights, and planner internals are not public settings. Hatchery's campaign values are `max_loaded_models=-1` and `memory_pressure_reclamation=automatic`; they are not product defaults.

### Accepted migration table

Every migration owns an independent journal, authority root, generation, exact input set, and source vector. `not_required` and `committed` are terminal; `prepared` is recomputed after source change or crash. Only the two named ambiguous migrations may enter `conflict`. A commit publishes through the shared configuration CAS, makes the owned legacy keys inert audit data, and changes later writes to HTTP 400 deprecated-key responses. Downgrade requires a quiesced authority export; there is no reverse live migration.

The common source vector includes the published root, relevant legacy and canonical configuration generations, affected recipe-option generations, startup generation, model-identity/alias generation, applicable topology generation, and the migration's own generation. Publication fails and recomputes when any member changes.

| Migration | Mapping | Conflict and cutover |
|---|---|---|
| `legacy_eviction_policy_v1` | Canonical `memory_pressure_reclamation` wins. Otherwise global `auto_evict=true` with only inherited/default per-model values maps to `automatic`; false/absent with defaults maps to `disabled`; any explicit/nondefault threshold or per-model input maps to `report_only` plus `residency_legacy_eviction_options_require_review`. | Deterministic; no `conflict`. Freeze global `auto_evict`, `auto_evict_threshold_pct`, and per-model `auto_evict`, `evict_idle_timeout`, `downsize_idle_timeout`, and `evict_weight_factor`. Before commit, legacy remains authoritative. |
| `legacy_memory_limit_v1` | Canonical `residency_memory_limit_gib` wins. Otherwise `max_gpu_memory_occupancy_gb=-1` maps to `null`; a positive value maps unchanged only when exactly one canonical GPU residency domain exists. | Zero or multiple domains enter `conflict`, emit `residency_legacy_memory_limit_ambiguous`, and refuse affected capacity admission until an exact-ETag admin resolution applies a value or `null`. |
| `legacy_dgpu_gtt_v1` | `enable_dgpu_gtt=false` is dropped. `true` emits `residency_legacy_gtt_hint_ignored` and grants no topology, signal, confidence, or capability authority. | Deterministic; no `conflict`; retires independently. |
| `legacy_pins_v1` | Canonicalize and deduplicate `pinned_models`; preserve unrelated recipe options while setting saved `pinned=true`; set startup policy true exactly when migrated membership is nonempty. | Saved true outside the list, saved false inside it, or explicit startup policy that changes membership enters `residency_pin_migration_conflict`. Legacy remains the sole saved-pin authority until one atomic root publishes recipe patches, startup policy, and target authority. |

### Campaign deployment binding

Synthetic NPC qualification and physical Hatchery collection use distinct immutable pre-evidence bindings. Before TASK-068 executes any synthetic case, TASK-067 freezes `npc_synthetic_evidence_binding/v1`: the exact relation-contract and gate-set digests; source/tree, generated-contract, implementation, harness, configuration, participant-manifest, case/direction/model-type/incumbent-state, fallback, issuer, witness, primary/backup evidence-authority, and administrative-independence identities; and `synthetic_only=true`, `runtime_authority=none`. TASK-080 verifies the binding as part of the inactive completeness candidate; after package verification, TASK-083 requires its material implementation and configuration closure to equal the release-verified closure from TASK-082. It contains no result, evidence index, catalog payload, authority binding, or other future reference.

Physical collection requires one immutable pre-execution `hatchery_deployment_binding/v1` object. It contains the campaign-contract digest; unpromoted catalog-candidate digest; promotion-unit IDs; exact pre-promotion fork Release asset locator, digest, and size; executable digest and size; dependency-closure, generated-resource, defaults, and release-verification manifest digests; model, helper, backend, and dependency artifact manifest digests whose entries each carry locator, digest, and size; primary GitHub Release authority and namespace; an independently administered backup provider ID, backup authority identity, administrative-boundary identity and signed independence proof, locator namespace, retention class, prospective roots, and authorization digest; witness provider, administrative boundary, conditional-append endpoint, authority identity, verification-key digest, and authorization digest; issuer key ID, public-key digest, and secret-source ID; clock, boot, daemon, and collection-environment identities; `sanitized_only=true`; and operator identity/time/authorization digest. It contains no run, bundle, promotion-index, or other future evidence reference. Model selections, contexts, thresholds, predictor revision, and workload partition live in the referenced campaign contract rather than being duplicated.

After collection and review, promotion publication uses strictly one-way layers and separates non-authorizing staging from the accepted authority head. First, immutable `promotion_catalog_candidate_payload/v1` and `promotion_evidence_index_payload/v1` objects contain the proposed capability changes and completed evidence DAG; each index payload references its catalog-candidate payload and applicable earlier `npc_synthetic_evidence_binding/v1` or `hatchery_deployment_binding/v1`, and neither payload type references a later authority object. Second, immutable `residency_promotion_authority_binding/v1` references the exact set of payload and pre-evidence-binding digests, exact primary/backup evidence asset identities, signed administrative-boundary proof, reference-liveness roots, issuer identity, and the prior tip of a non-authorizing candidate-witness namespace. Conditional append there returns a detached signed `promotion_candidate_receipt/v1`; neither object can change effective mode. Third, `promotion_publication_envelope/v1` references the complete payload set, authority binding, and candidate receipt. Each inactive packaged catalog entry carries direct backward references to its one applicable `promotion_evidence_index_payload/v1` digest and promotion-unit ID as well as the shared envelope digest. The package and evidence assets are uploaded, made publicly retrievable, and exact-verified before acceptance.

Acceptance is per promotion unit. In deterministic promotion-unit-ID order, one immutable `promotion_acceptance_record/v1` specializes the initial successful `EvidenceLivenessAttestation` for exactly one unit: it has one `promotion_index_digest` and a singleton `dependent_promotion_unit_ids` containing that unit. It contains every other mandatory accepted attestation field plus the envelope, candidate receipt, exact public package/evidence identities, and the prior accepted authority tip, but it never references the receipt returned for itself. Conditional append against that tip returns a detached signed `promotion_acceptance_receipt/v1`; only that unit may then receive its first lease and activate its catalog entry. The next unit's record uses the newly returned tip. A crash after a prefix commits leaves precisely that prefix accepted, all remaining entries inactive at their registered fallbacks, and restart resumes idempotently from the witnessed tip and next deterministic unit; it never rewrites or duplicates an accepted record. Later per-unit `EvidenceLivenessAttestation` records carry the same singular promotion-index digest and singleton dependent-unit list directly, plus the envelope and already-created acceptance-receipt identity. No fallible external artifact publication follows an accepted append, and no payload, binding, envelope, or acceptance record acquires a back-edge.

Missing, malformed, unauthorized, digest-mismatched, or independence-unproved fields stop before helper launch, model load, or collection and return the exact invalid field paths, failure classes, binding digest, and `collected=false`. A post-freeze change returns `new_campaign_revision_required`. Evidence that cannot be sanitized returns `private_evidence_authorization_required` and requires explicit private-store, backup, retention, access, derivation, and liveness-root authorization. An unhealthy store, witness, or issuer keeps every unit at fallback and issues no evidence lease or promotion.

### Implementation-base and phase-evidence manifest

TASK-005 creates `plan/portable-residency-implementation-base.json`; every later phase appends one immutable phase-exit record through `tools/validate_residency_implementation_handoff.py`. Schema `portable_residency_implementation_handoff/v1` requires the research baseline; stable release tag, public release URL, tag object when annotated, peeled commit, local and origin stable-ref commits; reconciled implementation-base commit; scout input/output digests; exact legacy callsite/symbol inventory; every source-seam disposition; created later-cell issue IDs; exact `pre_handoff_records` for TASK-001–TASK-004; one `implementation_base_bootstrap` record for TASK-005; and phase records containing task IDs, output digests, task-base commit, task-local red-fixture evidence commit/digest/command/failure signature, task green-checkpoint commit/tree and test result, fallback state, phase checkpoint commit/tree, and a digest over the canonical record payload excluding its digest field. The TASK-005 bootstrap record predates its validator, so it records its checkpoint/tree, outputs, validation results, and fallback state without claiming a red fixture. Each output digest binds the bytes at that task's recorded checkpoint; TASK-005 therefore binds the pre-phase-record manifest blob and never hashes the manifest containing its own record. Each pre-handoff record is explicitly classified `research_refresh`, names its output digests and validation commands/results in the planning checkpoint, and makes no retrospective red-to-green or implementation-evidence claim. Only `phase_records` become append-only after Phase 0; they are content-addressed.

Red-to-green evidence is reproducible without committing a broken branch state. Before each implementation, an isolated worktree at the recorded task-base commit adds only the public test/fixture and runs the recorded command to the frozen failure signature. A red-fixture evidence commit then stores the test-only patch plus environment and result metadata under `plan/evidence/red-fixtures/TASK-NNN/` while leaving the test unapplied in the source tree. The implementation starts from that evidence commit, applies the identical test patch, makes it pass, and lands the recorded task green checkpoint. At phase completion a final evidence-only commit appends the phase record and names its immediate-parent reviewed phase checkpoint commit/tree. `tools/validate_residency_implementation_handoff.py --phase N` verifies each red-fixture evidence commit descends directly from its recorded task base and changed only its evidence path; reconstructs task-base-plus-test in an isolated checkout and reproduces the failure; verifies the byte-identical test patch exists and passes at the task green checkpoint; verifies task and phase ancestry; checks every task output digest/fallback; and rejects later phase work when any record is missing or stale. No record or commit contains its own identity.

Stable v11.6.0 already provides the free `lemon::backends::commit_staged_install` primitive in `src/cpp/include/lemon/backends/install_staging.h`, used by `src/cpp/server/backends/backend_utils.cpp`. TASK-006 records and verifies that exact stable seam. TASK-047 adapts it behind the fork-owned `ArtifactWriterAdapter` and adds only the missing writer-session, identity, journal, quarantine, and verified-release semantics; it must not create a parallel install-commit authority.

### TDD and exact-output contract

Every implementation task after the TASK-005 bootstrap that changes code or generated data begins with the isolated failing public-seam test or byte fixture and committed red-fixture bundle defined above. Only then may it implement the task and record the green result. TASK-005 is the sole `implementation_base_bootstrap` exception because it creates the manifest before TASK-093 can implement its validator; it still records exact immutable inputs, outputs, checks, checkpoint/tree, and fallback state. TASK-001–TASK-004 are the already-completed pre-handoff research/source-closure refresh represented by `pre_handoff_records`; they are not runtime implementation, delivery, or promotion evidence and are not retroactively described as TDD work. Dedicated test tasks below are cross-unit fault, permutation, and compatibility matrices; they are never the first tests for preceding implementations. A task cannot be marked complete without its applicable exact output paths/symbols, evidence classification, registered fallback state, and phase exit command in the manifest.

| Tasks | Required output roots and public symbols |
|---|---|
| TASK-001–TASK-004 | `.github/workflows/docs_and_style.yml`; `docs/agents/{architecture-map,research-map}.md`; the portable-residency inventory, matrix, and campaign documents; inventory validator/source-closure modules and public-CLI tests; the reviewed planning checkpoint's exact ref-update and validation records. Classified only as pre-handoff research refresh. |
| TASK-005–TASK-007, TASK-093 | `plan/portable-residency-implementation-base.json`; content-addressed per-task bundles under `plan/evidence/red-fixtures/`; `tools/validate_residency_implementation_handoff.py`; `test/test_residency_implementation_handoff.py`; exact implementation-base scout outputs and created GitHub issue IDs recorded in the manifest. |
| TASK-008–TASK-013 | `tools/generate_residency_contract.py`; `src/cpp/resources/residency_profiles.json`; `src/cpp/include/lemon/residency/{types,generated_contract,catalog}.h`; `src/cpp/server/residency/{generated_contract,catalog,explanations}.cpp`; `docs/api/schemas/residency/`; `test/residency/contract/`. |
| TASK-014–TASK-018 | `test/residency/prototypes/`; content-addressed results under `docs/research/residency-prototype-results/`; no production authority. |
| TASK-019–TASK-025, TASK-094–TASK-095 | `src/cpp/include/lemon/residency/{journal,claims,recovery,recovery_store}.h`; `src/cpp/server/residency/{journal,claims,recovery,recovery_store}.cpp`; `src/cpp/server/residency/platform/`; `test/residency/recovery/`. |
| TASK-026–TASK-030 | `src/cpp/include/lemon/residency/provider.h`; `src/cpp/server/residency/providers/{system_provider,backend_provider}.cpp`; `test/residency/{fakes,providers}/`. |
| TASK-031–TASK-042 | `src/cpp/include/lemon/residency/planner.h`; `src/cpp/server/residency/planner.cpp`; one file per operation under `src/cpp/server/residency/plans/`; `src/cpp/server/residency/coordinator.cpp`; `test/residency/planner/`. |
| TASK-043–TASK-046, TASK-072–TASK-075, TASK-105–TASK-106 | `src/cpp/include/lemon/{router,wrapped_server}.h`; `src/cpp/server/{router,residency/lifecycle,residency/cutover,residency/ownership_universe}.cpp`; `test/residency/{lifecycle,cutover}/`. The current fork entry is `lemon::plan_gpu_memory_admission`; stable symbols are recorded by TASK-006. |
| TASK-047–TASK-049, TASK-099–TASK-101 | `src/cpp/include/lemon/residency/artifact_writer.h`; `src/cpp/server/residency/{artifact_writer,staged_import,artifact_delete,artifact_cleanup,service_shutdown}.cpp`; `test/residency/artifacts/`. |
| TASK-050–TASK-053, TASK-096–TASK-098, TASK-102 | `src/cpp/resources/defaults.json`; `src/cpp/include/lemon/runtime_config.h`; `src/cpp/server/{runtime_config,residency/configuration,residency/migration}.cpp`; `test/residency/configuration/`. |
| TASK-054–TASK-061, TASK-103 | `src/cpp/server/server.cpp`; generated route/auth/projector tables; CLI/tray/shared-renderer adapters; `test/residency/api/` and existing endpoint suites. |
| TASK-062–TASK-066 | `src/cpp/server/residency/{pressure_monitor,pressure_controller,pressure_executor}.cpp`; `test/residency/pressure/`. |
| TASK-067–TASK-071 | `tools/residency_npu_contract/`; `src/cpp/server/residency/evidence_liveness.cpp`; `tools/residency_evidence/`; `test/residency/{npu,evidence}/`. |
| TASK-076–TASK-080, TASK-104 | `src/cpp/server/residency/providers/hatchery_rocm_provider.cpp`; `src/cpp/server/residency/predictors/llamacpp_rocm.cpp`; `src/cpp/server/residency/delivery_verifier.cpp`; `test/residency/delivery/`. |
| TASK-081–TASK-087, TASK-092, TASK-107 | platform package/release manifests; `tools/residency_campaign/`; promoted catalog and evidence assets; `test/residency/release/`. |
| TASK-088–TASK-091 | exact later-cell output roots from the TASK-007 issue manifests; `src/app/src/renderer/` presentation adapters; retained-boundary removal diffs in `src/cpp/server/`; platform build and artifact manifests; `docs/research/portable-residency-final-report.md`; `test/residency/release/`. |

## 2. Implementation Steps

All new C++ test executables must be registered with `add_cpp_ci_test(... CI ON ...)`. Every phase that contains C++ work begins from:

```bash
cmake --preset debug -DBUILD_TESTING=ON
cmake --build --preset debug --target cpp-ci-tests
```

### Implementation Phase 0: Freeze research closure and the stable implementation base

- **GOAL-001**: Keep upstream-main research classification distinct from the stable runtime base, then freeze both before implementation.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-001 | — | Refresh `upstream/main`, `upstream-main`, and `origin/upstream-main` to `a505bbc702cc1fcd44ef73c44defabc98c36d505` through the exact-lease workflow. | ✅ | 2026-08-14 |
| TASK-002 | TASK-001 | Update the inventory baseline and exact transitive source closure for support descriptors, model types, registry artifact selection, backend dispatch, selector overrides, and generic GGUF matching. | ✅ | 2026-08-14 |
| TASK-003 | TASK-002 | Extend source validation and public-CLI tests so a changed, added, removed, or relocated artifact selector fails closed. | ✅ | 2026-08-14 |
| TASK-004 | TASK-003 | Regenerate the matrix and campaign projections; prove support atoms, variants, exclusions, promotion units, fallbacks, and gates are unchanged. | ✅ | 2026-08-14 |
| TASK-005 | TASK-004 | At implementation start, query the live GitHub Releases API; verify the selected tag and peeled commit; advance `upstream-stable` and `origin/upstream-stable`; reconcile stable without rewriting upstream identity; and create the implementation-base manifest. The live 2026-08-15 selection is lightweight tag `v11.6.0` at commit `93aac619a9df474ae1782f4bd45a0a147d6dd4b5`; both stable refs now resolve there. The live query remains authoritative. | | |
| TASK-093 | TASK-005 | Add failing public-CLI fixtures, then implement `tools/validate_residency_implementation_handoff.py` and schema validation for release/tag/ref identity, implementation base, scout outputs, source-seam dispositions, issue IDs, task outputs, isolated red-fixture reconstruction, the green-checkpoint/evidence-commit relation, and append-only phase records. | | |
| TASK-006 | TASK-093 | Re-scout router eviction, `plan_gpu_memory_admission`, stable `EvictionEngine`/`GlobalVramMonitor`, backend process hooks, artifact writers, configuration, API mappings, and tests on the reconciled implementation base. Include the routing-helper prune/reclaim/executor and pending-stale lifecycle introduced by `9a8b2c99b975690dda56d6f1847b7b2bb35fbcaa`. Record exact paths/symbols and amend this plan only for changed source facts, not new policy. | | |
| TASK-007 | TASK-006 | Create separate implementation/qualification issues for Hatchery Vulkan, Windows XDNA2 participant runtime cells, and every remaining exact platform/backend/operation unit in the frozen roster. Each issue names one selector, recovery profile, fallback, delivery gate, and evidence gate set. | | |

Exit gate:

```bash
git merge-base --is-ancestor origin/upstream-stable HEAD
test "$(git rev-parse upstream-stable)" = "$(git rev-parse origin/upstream-stable)"
python3 tools/validate_residency_capability_inventory.py
python3 -m unittest discover -s test -p 'test_residency_capability_inventory_*.py' -q
ctest --test-dir build-debug -L cpp-ci --output-on-failure
python3 tools/validate_residency_implementation_handoff.py --phase 0
```

The exit record includes both immutable SHAs and the created issue IDs. No runtime task may begin before this gate passes.

### Implementation Phase 1: Generate the inactive executable contract

- **GOAL-002**: Turn the machine inventory into versioned runtime types, reason registries, and packaged resources without granting mutation authority.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-008 | TASK-007 | Define closed promotion-unit, operation, constraint, capability, delivery, effective-mode, footprint-confidence, reason, phase, outcome, fallback, and schema types. Unknown values deserialize as non-authorizing results. | | |
| TASK-009 | TASK-008 | Add `tools/generate_residency_contract.py` to generate the catalog resource, C++ bindings, operation/reason/presentation/retention/HTTP/auth registries, JSON schemas, and byte-golden fixtures from one source. | | |
| TASK-010 | TASK-009 | Implement exact catalog parsing and selector resolution; reject missing, duplicate, ambiguous, precedence-incomparable, digest-mismatched, or unsupported matches. | | |
| TASK-011 | TASK-009 | Implement the immutable explanation store and bounded unknown-minor rendering from generated types; explanations remain projections rather than lifecycle or claim authority. | | |
| TASK-012 | TASK-010, TASK-011 | Wire deterministic generation, double-generation byte checks, packaging, and CI while keeping every promotion unit `unsupported` and `absent`. | | |
| TASK-013 | TASK-012 | Add cross-component schema/generator/selector/fallback/reason/presentation, unknown-kind, duplicate-key, packaging, and byte-drift matrices. Each preceding implementation task retains its own test-first public-seam fixture. | | |

Exit gate:

```bash
python3 tools/generate_residency_contract.py --check
ctest --test-dir build-debug -R '^ResidencyContract' --output-on-failure
git diff --check
python3 tools/validate_residency_implementation_handoff.py --phase 1
```

The generated catalog must report every unit as `delivery_state=absent`; no new route or automatic action is active.

### Implementation Phase 2: Prove mechanism feasibility

- **GOAL-003**: Resolve source-level feasibility before depending on platform mechanisms.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-014 | TASK-013 | Prototype crash-durable root publication and non-PID process containment/ownership on POSIX and Windows, including descendant/job membership and verified termination. | | |
| TASK-015 | TASK-013 | Prototype causal GTT, host-memory, process, cgroup, and device attribution on Hatchery; record every unavailable projection as unknown rather than zero. | | |
| TASK-016 | TASK-013 | Prototype the pure-offline llama.cpp/ROCm sizing path and its own complete operation manifest; prove no backend spawn, device-context initialization, or unreserved driver effect. | | |
| TASK-017 | TASK-013 | Prototype verified soft release for the available llama.cpp mechanisms, including causal identity, physical release, ledger reconciliation, and unsupported-mechanism fallback. | | |
| TASK-018 | TASK-013 | Prototype FLM service membership and ownership boundaries without claiming Windows participant-cell or physical NPU authority. | | |

Exit gate:

```bash
ctest --test-dir build-debug -R '^ResidencyPrototypeContract' --output-on-failure
python3 tools/validate_residency_implementation_handoff.py --phase 2
```

Each prototype publishes a pass record or the exact catalog fallback/deferred-cell disposition. A failed platform prototype blocks only mechanisms that depend on it and does not invent policy.

### Implementation Phase 3: Establish generic durable authority and recovery

- **GOAL-004**: Build the journal, claims, readiness, quarantine, and recovery protocols with generic interfaces and fakes.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-019 | TASK-013 | Define journal records, authority roots, claim families, resident states, recovery dispositions, and quarantine origins with schema, sequence, predecessor, daemon epoch, operation identity, complete claim closure, and checksum. | | |
| TASK-020 | TASK-014, TASK-019 | Implement crash-safe append, fsync, atomic root publication, replay, compaction, and downgrade export through platform durable-file adapters. | | |
| TASK-021 | TASK-019 | Implement checked nonnegative claim algebra and current, provisional, retained, quarantine, and conservative-overlay projections. | | |
| TASK-094 | TASK-019 | Define and implement the content-addressed recovery-origin store with durable hash verification before references, transactional reachability/tombstone/deleting states, safe revival/GC, missing-object corruption fencing, and topology/catalog rebind. | | |
| TASK-095 | TASK-020, TASK-094 | Add crash and fault matrices for object write/reference ordering, hash mismatch, missing objects, concurrent revival/GC, tombstone/deleting recovery, rebind, and journal compaction. | | |
| TASK-022 | TASK-014, TASK-019 | Define recovery-profile interfaces for prepared launch, containment, ownership, and verified release. Implement fakes plus the common protocol only; do not claim every real platform/profile. | | |
| TASK-023 | TASK-020, TASK-021, TASK-022, TASK-094, TASK-095 | Implement daemon exclusion, object-verified replay-before-readiness, maximum-claim quarantine, bounded hierarchical incident controls, and origin-specific clear tables. | | |
| TASK-024 | TASK-023 | Implement generic process-manager hooks for prepared identities and ownership attestations while leaving unsupported concrete profiles at fallback. | | |
| TASK-025 | TASK-020, TASK-023, TASK-024 | Add deterministic crashes at every publication/effect boundary, malformed/truncated records, PID reuse, second-daemon exclusion, unknown survivors, compaction, and recovery tests. | | |

Exit gate:

```bash
ctest --test-dir build-debug -R '^Residency(Journal|Claims|RecoveryStore|RecoveryCore)' --output-on-failure
python3 tools/validate_residency_implementation_handoff.py --phase 3
```

### Implementation Phase 4: Build providers, atomic planners, and reservations

- **GOAL-005**: Produce complete provenance-bound facts, deterministic per-operation plans, and atomic reservations before dispatch.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-026 | TASK-013, TASK-021 | Define provider, topology, observation, effect, allocation-group, and verified-release envelopes with identity, domains, generation, time, freshness, uncertainty, provenance, and completeness. | | |
| TASK-027 | TASK-026 | Implement generic system topology and host-memory providers; represent shared-GTT aliasing and provider-resolved capacity explicitly. | | |
| TASK-028 | TASK-024, TASK-026 | Implement backend/process facts for executable, plugins/libraries, loader resolution, driver/runtime ABI, device identity, and the exact registry-selected active artifact snapshot. | | |
| TASK-029 | TASK-026 | Add deterministic fake topology, observation, backend, process, clock, witness, and filesystem providers. | | |
| TASK-030 | TASK-027, TASK-028, TASK-029 | Test stale/skewed samples, aliases, incomplete domains, disappearing processes, dependency drift, mechanism failure, release mismatch, and provenance changes. | | |
| TASK-031 | TASK-010, TASK-021, TASK-026 | Define complete lifetime manifests, plan inputs/tokens, reservations, actions, victims, protection state, and terminal dispositions. | | |
| TASK-032 | TASK-031 | Implement the policy-free planner kernel: manifest validation, exact selector resolution, checked arithmetic, confidence/effective-mode checks, count ceilings, protection predicates, stable ordering, and immutable token format. | | |
| TASK-033 | TASK-032 | Implement the complete ADM template, including discovery/growth effects and unknown-capacity refusal. | | |
| TASK-034 | TASK-033 | Implement the complete LFR template, preserving the original failure and requiring victim/release proof before one bounded retry. | | |
| TASK-035 | TASK-032 | Implement the PRE deficit and action-set planner, soft/hard eligibility, one-action rule, causal exclusions, and registered fallbacks. | | |
| TASK-036 | TASK-033 | Implement connected-group STA reservation, all-or-none provisional commit, and rollback. | | |
| TASK-037 | TASK-023, TASK-032 | Implement every REC leaf through common quarantine, ownership, and verified-release authority. | | |
| TASK-038 | TASK-032 | Implement explicit and authorized force UNL with resident-use fencing and complete disposition. | | |
| TASK-039 | TASK-032 | Implement saved-pin, runtime-pin, legacy-batch, and pin-recovery leaves without conflating preference with residency. | | |
| TASK-040 | TASK-032 | Implement the evidence-only NPC relation evaluator: exact Windows XDNA2 pairs/directions/model types/states, preserve every incumbent, refuse pre-spawn, and grant no capacity/PRE/recovery authority. | | |
| TASK-041 | TASK-033, TASK-034, TASK-035, TASK-036, TASK-037, TASK-038, TASK-039, TASK-040 | Implement atomic reservation publication, generation close, old-generation reconciliation, and irreversible-edge revalidation in the coordinator. | | |
| TASK-042 | TASK-030, TASK-041 | Add property, permutation, overflow, concurrency, stale-token, protection, and fallback tests for every operation. | | |

Exit gate:

```bash
ctest --test-dir build-debug -R '^Residency(Provider|Planner|Coordinator)' --output-on-failure
python3 tools/validate_residency_implementation_handoff.py --phase 4
```

### Implementation Phase 5: Integrate inactive lifecycle, artifact, configuration, and projection façades

- **GOAL-006**: Complete every authority surface in shadow/pass-through mode before any exclusive cutover.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-043 | TASK-025, TASK-041 | Add the coordinator to `Router` in observation-only pass-through mode. It may compare legacy and target outcomes but cannot reserve, evict, terminate, or commit. | | |
| TASK-044 | TASK-043 | Map the current fork `lemon::plan_gpu_memory_admission` in `gpu_memory_planner.{h,cpp}`, direct `Router::load_model` eviction/retry, stable `EvictionEngine`/`GlobalVramMonitor`, `WrappedServer` downsize/watchdog hooks, and every legacy mutation callsite to one target operation/fallback and cutover generation. | | |
| TASK-045 | TASK-024, TASK-028 | Add prepared launch, ownership, effect, observation, and verified-release hooks to `WrappedServer`; unimplemented hooks select fallback. Implement only generic interfaces here. | | |
| TASK-046 | TASK-041, TASK-045 | Implement provisional load, claim transfer, resident-use leases, commit, rollback, unload authorization, and dead-backend pruning behind inactive coordinator façades. | | |
| TASK-047 | TASK-023, TASK-041 | Route `Server::handle_pull`, Ollama/public pulls, `ModelManager::download_model`, and background jobs through ordered artifact-writer, identity, and lifecycle fences. Adapt stable `lemon::backends::commit_staged_install` behind the fork-owned `ArtifactWriterAdapter`; preserve its single commit/rollback authority and add only the missing writer-session, identity, journal, quarantine, and verified-release contract. | | |
| TASK-048 | TASK-047 | Implement staged import/adoption with manifest validation, registration eligibility, identity reservation, writer quiescence, atomic publication, expiry, abort, and quarantine. | | |
| TASK-049 | TASK-046, TASK-047, TASK-048 | Route model unregister/delete and Ollama delete through writer fences, complete binding closure, verified artifact/identity release, and quarantine while preserving status/body/auth behavior. | | |
| TASK-099 | TASK-047, TASK-048 | Route cache cleanup through writer/session reachability, retention roots, quarantine, and verified deletion without removing referenced or ambiguous state. | | |
| TASK-100 | TASK-046, TASK-047, TASK-048 | Route service shutdown through ordered admission closure, writer quiescence, lifecycle disposition, verified process/resource release, and durable recovery handoff. | | |
| TASK-050 | TASK-013 | Add the exact public defaults and validation; keep internal controller and sensor values non-public. | | |
| TASK-051 | TASK-020, TASK-050 | Implement source-vector configuration authority, strong ETag `{set,reset}` CAS, unrelated-path preservation, and generation-bound reconfiguration closure. | | |
| TASK-052 | TASK-051 | Implement `legacy_eviction_policy_v1` with its exact frozen inputs, deterministic mapping, independent root, cutover, deprecated-key behavior, and downgrade export. | | |
| TASK-096 | TASK-051 | Implement `legacy_memory_limit_v1` with domain-count conflict, admin resolution, independent root, cutover, and downgrade export. | | |
| TASK-097 | TASK-051 | Implement deterministic `legacy_dgpu_gtt_v1` hint retirement and diagnostic without granting evidence authority. | | |
| TASK-098 | TASK-039, TASK-051 | Implement `legacy_pins_v1` with canonical membership, preserved recipe options, startup policy, conflict resolution, atomic root, and downgrade export. | | |
| TASK-053 | TASK-036, TASK-039, TASK-051, TASK-098 | Implement grouped startup planning and atomic provisional commit; failure loads none and preserves preferences/explanations. | | |
| TASK-054 | TASK-011, TASK-043, TASK-046, TASK-047, TASK-048, TASK-049, TASK-050, TASK-051, TASK-052, TASK-053, TASK-096, TASK-097, TASK-098, TASK-099, TASK-100 | Register canonical residency, operation-detail, configuration, pin-preference, and runtime-pin routes under all four prefixes with generated auth and HTTP mappings. | | |
| TASK-055 | TASK-011, TASK-054 | Project the same explanation revision into structured JSON logs only. | | |
| TASK-056 | TASK-011, TASK-054 | Project canonical state into health/status, `/params`, `/internal/set`, and retained legacy-route adapters without reconstructing policy. | | |
| TASK-057 | TASK-011, TASK-054 | Implement Ollama-compatible model/status projections with exact response preservation. | | |
| TASK-058 | TASK-011, TASK-054 | Implement the typed CLI transport and CLI rendering. | | |
| TASK-059 | TASK-011, TASK-054 | Implement the typed tray-state adapter and rendering. | | |
| TASK-060 | TASK-011, TASK-054 | Implement the shared desktop/web data adapter only; visual design remains TASK-089. Preserve separate packaging smoke tests. | | |
| TASK-061 | TASK-044, TASK-045, TASK-046 | Add cross-unit lifecycle concurrency, process-creation, cancellation, backend-death, retry, descendant-cleanup, and claim-transfer matrices while proving the target coordinator remains inactive. | | |
| TASK-101 | TASK-047, TASK-048, TASK-049, TASK-099, TASK-100 | Add artifact acquisition/import/delete/cleanup/shutdown fault and compatibility matrices, including writer races and quarantine. | | |
| TASK-102 | TASK-050, TASK-051, TASK-052, TASK-053, TASK-096, TASK-097, TASK-098 | Add configuration/migration ETag, stale-writer, conflict, crash, downgrade, grouped-startup, and pin-separation matrices. | | |
| TASK-103 | TASK-054, TASK-055, TASK-056, TASK-057, TASK-058, TASK-059, TASK-060 | Add quad-prefix, auth, retention, schema, compatibility, and cross-client explanation-revision matrices. | | |

Exit gate:

```bash
ctest --test-dir build-debug -R '^Residency(Lifecycle|Artifact|Configuration|Migration|Api)' --output-on-failure
python3 tools/validate_residency_implementation_handoff.py --phase 5
```

### Implementation Phase 6: Complete pressure, NPU synthetic, and evidence subsystems

- **GOAL-007**: Finish independent subsystems while the legacy runtime authority remains exclusive.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-062 | TASK-026, TASK-043 | Implement an event-coalescing and bounded-polling pressure monitor that publishes coherent episodes without acting. | | |
| TASK-063 | TASK-017, TASK-038, TASK-062 | Implement soft mechanisms, `last_became_idle_at`, warm/cold strata, and verified soft-release outcomes. | | |
| TASK-064 | TASK-035, TASK-062, TASK-063 | Implement dwell, critical confirmation, stabilization, fair epochs, backoff, latched domains, exact action-set scoring, action leases, and causal exclusion of unrelated demand. | | |
| TASK-065 | TASK-041, TASK-064 | Implement one-action execution and atomic `complete_action_and_close_plan`; reconcile physical and ledger release before episode recovery. | | |
| TASK-066 | TASK-065 | Implement disabled/report-only modes, critical fences, resident-use noninterference, unresolved-pressure explanations, and deterministic PRE tests. | | |
| TASK-069 | TASK-020, TASK-011 | Implement per-unit signed evidence-liveness attestations carrying one `promotion_index_digest`, singleton `dependent_promotion_unit_ids`, and publication-envelope identity; an initial acceptance-record form that contains no current receipt; later forms that carry the already-created acceptance-receipt identity; monotonic anti-rollback heads; separate non-authorizing candidate and accepted witness appends; deterministic prefix recovery; renewable daemon-bound leases; authority caps; and restart/suspend invalidation. | | |
| TASK-070 | TASK-069 | Implement sanitized content-addressed evidence publication with primary and independent backup locators, reference-liveness roots, and local-only runtime observations. | | |
| TASK-067 | TASK-040, TASK-061, TASK-070, TASK-103 | Build the synthetic NPC harness for both Windows XDNA2 FLM↔exclusive pairs, both directions, all declared model types, and unpinned-idle/pinned/in-use incumbents, then freeze the complete `npc_synthetic_evidence_binding/v1` before executing a case. | | |
| TASK-068 | TASK-067 | Run the exact NPC gate set and build an inactive reviewed compatibility evidence candidate; failure preserves/refuses and grants no runtime authority. | | |
| TASK-071 | TASK-068, TASK-070 | Test NPC gates, signatures, fork/rollback, witness unavailability, expiry, suspend/resume, restart, locator loss, redaction, and fallback behavior. | | |

Exit gates:

```bash
ctest --test-dir build-debug -R '^ResidencyPressure' --output-on-failure
ctest --test-dir build-debug -R '^ResidencyNpuCompatibility' --output-on-failure
ctest --test-dir build-debug -R '^Residency(EvidenceLiveness|EvidencePublication)' --output-on-failure
python3 tools/validate_residency_implementation_handoff.py --phase 6
```

### Implementation Phase 7: Prepare one exclusive authority cutover

- **GOAL-008**: Build and test the cutover transaction without publishing it before exact delivery verification.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-072 | TASK-044, TASK-061, TASK-066, TASK-101, TASK-102, TASK-103 | Freeze the exhaustive old-to-new authority table and prove every automatic mutation callsite has exactly one new operation or compatibility fallback. | | |
| TASK-073 | TASK-052, TASK-061, TASK-072, TASK-096, TASK-097, TASK-098, TASK-102 | Prepare and validate all configuration/migration roots and compatibility adapters without publishing the cutover generation. | | |
| TASK-105 | TASK-023, TASK-046, TASK-073 | Implement the ownership-universe subprotocol used only inside a bound `legacy_closing` generation: enumerate every known legacy resident and plausible orphan through a reviewed discovery profile, forbid adoption, verify-release known residents or transfer them to non-routable cleanup/quarantine records with persisted recovery origin and maximum claims, and require the closed discovered universe to equal the replayed journal projection before readiness. | | |
| TASK-106 | TASK-105 | Add live-legacy resident, unrecorded process, plausible orphan, PID reuse, discovery crash, quarantine, restart, projection-mismatch, and scan→legacy-spawn→enable race fixtures. | | |
| TASK-074 | TASK-066, TASK-073, TASK-105, TASK-106 | Implement, but do not publish in production, one durable per-operation cutover transaction. Atomically publish `legacy_closing` to reject new legacy lifecycle mutations; drain or disposition every in-flight effect; run the ownership-universe subprotocol; disable `plan_gpu_memory_admission`, stable `EvictionEngine`/`GlobalVramMonitor`, direct NPU/LRU/nuclear retry, downsize no-op success, and watchdog direct termination; then CAS-revalidate the closing generation and immutable universe root before enabling the matching coordinator operation. On failure the target remains disabled and the transaction either publishes a fresh legacy generation before any target effect or stays safely closed for recovery. | | |
| TASK-075 | TASK-074 | Inject crashes before and after every fixture close/drain/scan/disposition/revalidate/enable boundary and prove exactly one authority recovers. Keep the production cutover unpublished and legacy authority exclusive. | | |

Exit gate:

```bash
ctest --test-dir build-debug -R '^Residency(SingleAuthority|OwnershipUniverse|Cutover|Compatibility)' --output-on-failure
python3 tools/validate_residency_implementation_handoff.py --phase 7
```

The gate asserts the production authority root still names the legacy engine.

### Implementation Phase 8: Complete exact delivery candidates

- **GOAL-009**: Implement complete first-profile units without changing accepted capability.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-076 | TASK-014, TASK-024, TASK-075 | Implement the concrete Hatchery Linux `native_subprocess_tree` recovery profile. Windows/macOS native trees, async-job trees, and FLM service membership remain fallback-only for their later exact cells. | | |
| TASK-077 | TASK-015, TASK-027, TASK-028, TASK-076 | Implement the exact Hatchery shared-GTT provider with GTT, `MemAvailable`, topology, process/cgroup identity, freshness, skew, attribution, and verified release. | | |
| TASK-078 | TASK-016, TASK-077 | Implement the pure-offline llama.cpp/ROCm estimator bound to the exact backend/dependency/configuration predicate and explicit exclusions. | | |
| TASK-104 | TASK-013, TASK-075 | Implement one generated completeness verifier for runtime cells and evidence-only contracts; it checks exact outputs, fallbacks, gates, recovery identity where applicable, tests, and inactive authority. | | |
| TASK-079 | TASK-076, TASK-077, TASK-078, TASK-104 | Run the shared verifier over each Hatchery ADM/PRE/STA/REC unit and publish only complete units into an inactive reviewed candidate as `implemented_unverified`; partial units stay `absent`. | | |
| TASK-080 | TASK-068, TASK-104 | Run the shared verifier over the evidence-only NPC unit and its immutable synthetic binding; it remains `runtime_authority=none`. | | |

Exit gate:

```bash
ctest --test-dir build-debug -R '^Residency(HatcheryDelivery|NpuContractDelivery)' --output-on-failure
python3 tools/validate_residency_implementation_handoff.py --phase 8
```

### Implementation Phase 9: Verify pre-promotion release delivery

- **GOAL-010**: Advance delivery only after verifying an inactive pre-promotion package and its complete identity closure.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-081 | TASK-071, TASK-079, TASK-080 | Add source/catalog/generated-resource/executable/dependency/defaults digests and candidate identities to release manifests and every platform package. | | |
| TASK-082 | TASK-081 | Build the inactive pre-promotion artifacts, install/archive them, verify package contents and identities, publish them as content-addressed assets on an explicit fork pre-release, and record the exact GitHub Release authority, locator, digest, and size in the reviewed unpromoted catalog candidate. Advance each complete unit to `release_verified` without changing capability. Failure removes or invalidates the candidate and leaves its prior delivery state/fallback. | | |

Exit gate:

```bash
ctest --test-dir build-debug -R '^Residency(Packaging|ReleaseVerification)' --output-on-failure
python3 tools/validate_residency_implementation_handoff.py --phase 9
```

### Implementation Phase 10: Qualify NPU synthetically and Hatchery physically

- **GOAL-011**: Change capability only through the exact evidence gate for each release-verified promotion unit.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-083 | TASK-071, TASK-082 | Require exact equality between the NPC binding's material closure and TASK-082, then review the synthetic evidence candidate and, if every exact gate passes, publish its immutable promotion-index and catalog-candidate payloads proposing only `H-NPU-FLM-CONFLICT-XDNA2-v1` at `modeled`; otherwise preserve its prior capability and preserve/refuse fallback. The evidence-index payload points backward to `npc_synthetic_evidence_binding/v1`; neither payload references a later authority binding or receipt. | | |
| TASK-084 | TASK-017, TASK-077, TASK-078, TASK-082 | Build the Hatchery campaign harness with an immutable operator-authorized isolated `calibrated_instance` authority root, HIP/host helpers, watchdogs, named barriers, frozen actor rules, deterministic partitions, raw traces, sanitization, evidence indexes, and machine gates. Production remains on legacy/fallback authority and the live catalog remains unpromoted during collection. | | |
| TASK-085 | TASK-084 | Freeze and validate the complete deployment binding defined above. On failure, stop before collection and return the exact missing/invalid fields and `collected=false`. | | |
| TASK-086 | TASK-085 | Run topology, footprint, ADM, PRE, STA, REC, protection, concurrency, liveness, explanation, release, and recovery gates with three runtime-cold attempts across three boot-cold epochs. Retain every failure/invalid bundle under frozen rules. | | |
| TASK-087 | TASK-086 | Publish one reviewed immutable promotion-index payload and catalog-candidate payload per passing Hatchery cell, each pointing only backward to its deployment binding and completed evidence DAG. Keep failing/incomplete cells at fallback and permanently invalidate a predictor revision after any valid bound exceedance. These payloads do not reference a later authority binding or receipt. | | |
| TASK-107 | TASK-083, TASK-087 | Stage one new product-and-evidence release unit containing the proposed catalog payloads and the byte-identical executable/backend/dependency/generated-contract/configuration/default closure from TASK-082/TASK-085. Upload primary evidence to the draft consuming fork release and copy/verify it under the authorized independent backup authority. Create the one-way promotion-authority binding, append it only to the non-authorizing candidate witness namespace, and build the envelope. Package every catalog entry inactive with its direct promotion-index/unit references plus the envelope, then upload, publish, retrieve, and exact-verify the public package and evidence assets. Only after all those checks pass, create and conditionally append one acceptance record per unit in deterministic ID order, keeping each returned checkpoint only in its detached receipt and activating only that unit through its liveness head. Failure before the first accepted append leaves the prior accepted tip unchanged; a crash after a prefix uses the witness record and already-published immutable assets to preserve the prefix and resume at the next inactive unit. Only machine-verified `release_container_delta/v1` package metadata and catalog/envelope reference changes are non-behavioral. Any other material identity change, detected before packaging and rechecked before every accepted append, invalidates TASK-082 and every affected not-yet-accepted payload: rerun the affected TASK-067–TASK-080 implementation/evidence/delivery gates, TASK-081/TASK-082 exact-package verification, TASK-083 NPC promotion, and TASK-084–TASK-087 Hatchery campaign with a new deployment binding before re-entering TASK-107; already accepted units retain their immutable prior revision and are never rebound to changed bytes. | | |
| TASK-092 | TASK-075, TASK-106, TASK-107 | Execute the TASK-074 close/drain/scan/disposition/revalidate transaction in production separately for each promoted and release-reverified runtime operation, disabling its complete legacy authority before enabling the coordinator generation. Permit legacy rollback only before any new-generation effect or after quiesced authority export. The evidence-only NPC contract performs no runtime cutover. | | |

Exit gates:

```bash
ctest --test-dir build-debug -R '^ResidencyNpuCompatibilityPromotion' --output-on-failure
ctest --test-dir build-debug -R '^Residency(PromotedRelease|ProductionCutover)' --output-on-failure
python3 -m unittest discover -s tools/residency_campaign -p 'test_*.py' -q
python3 tools/validate_residency_capability_inventory.py
python3 tools/validate_residency_implementation_handoff.py --phase 10
```

### Implementation Phase 11: Extend the matrix and retire compatibility code

- **GOAL-012**: Reuse only release-verified core mechanics while keeping every later cell independently fallback-bound and qualified.

| Task | Depends on | Description | Completed | Date |
|---|---|---|---|---|
| TASK-088 | TASK-007, TASK-107 | Implement later providers, predictors, recovery profiles, catalog records, and campaigns through their own issues and exact gates; never transfer ROCm/shared-GTT/NPC evidence. | | |
| TASK-089 | TASK-054, TASK-058, TASK-059, TASK-060, TASK-107 | Add reviewed client presentation for release-verified server states using `DESIGN.md` and `.impeccable/design.json`; keep server truth authoritative and preserve desktop/web package separation. | | |
| TASK-090 | TASK-088, TASK-092 | Remove superseded router eviction/retry/startup-pin/legacy mutation code only after every retained boundary has a generated adapter, announced retirement, quiesced export rule, and regression coverage. | | |
| TASK-091 | TASK-087, TASK-088, TASK-089, TASK-090 | Run the final Windows/Linux/macOS build, artifact audit, migration and rollback rehearsals, documentation validation, and per-cell capability report. | | |

Exit gate:

```bash
cmake --build --preset debug --target cpp-ci-tests
ctest --test-dir build-debug -L cpp-ci --output-on-failure
python3 tools/validate_residency_capability_inventory.py
git diff --check
python3 tools/validate_residency_implementation_handoff.py --phase 11
```

## 3. Alternatives

- **ALT-001**: Extend the existing `Router` with more platform and backend conditionals. Rejected because it preserves split policy, bypasses complete manifests and recovery authority, and cannot produce one auditable per-operation explanation.
- **ALT-002**: Use `max_loaded_models` or file-size estimates as the universal safety mechanism. Rejected because count and file size do not bound shared GPU, host, driver, KV, compute, retained, or concurrent lifetime effects.
- **ALT-003**: Discover memory demand by spawning the real backend before reservation. Rejected because discovery itself can allocate unreserved host, device, driver, and process resources.
- **ALT-004**: Promote a predictor family or platform profile globally after one successful campaign. Rejected because authority belongs to one reviewed catalog revision, complete manifest, exact selector, operation, and gate set.
- **ALT-005**: Learn catalog authority automatically from runtime observations. Rejected because local observations may inform future reviewed calibration but cannot self-promote or redistribute evidence.
- **ALT-006**: Implement all platforms in one release before qualifying Hatchery. Rejected because it produces an unreviewable cutover and prevents safe fallback-only checkpoints.
- **ALT-007**: Store catalog, physical evidence, and runtime observations in one mutable data file. Rejected because release configuration, immutable proof, and local health have different trust, retention, and distribution boundaries.

## 4. Dependencies

- **DEP-001**: Upstream research/source-closure baseline `a505bbc702cc1fcd44ef73c44defabc98c36d505`, every blob declared by `source_file_blobs`, and every tree declared by `source_tree_objects` must remain available and independently type-checked by validation and CI.
- **DEP-002**: The accepted policy spine in `CONTEXT.md`, `docs/agents/architecture-map.md`, and the five residency research documents is normative for implementation behavior.
- **DEP-003**: Cross-platform durable file publication, process containment, process identity, and verified-release primitives must be available for Windows, Linux, and macOS before the corresponding runtime cell can become release-verified.
- **DEP-004**: The exact llama.cpp/ROCm release artifact, source/build attestation, dependency closure, device/driver identity, and v1 configuration predicate must be frozen before Hatchery predictor calibration.
- **DEP-005**: The Hatchery campaign requires controlled hardware access, the native HIP helper, host helper, watchdog, model artifacts, adequate quiescent capacity, an independent evidence backup, an anti-rollback witness, and an issuer key.
- **DEP-006**: Hosted CI must build the generated contract, run cross-platform compiled tests, verify packaged resources, and run the Python inventory suite from the exact upstream object closure.
- **DEP-007**: Physical promotion requires a reviewed immutable evidence index and catalog revision; Ralph and human review verify conformance/publication but do not replace independent physical evidence.
- **DEP-008**: Runtime implementation requires a separately recorded, live-verified stable-upstream implementation base and a green post-reconciliation source scout. The research baseline never substitutes for this gate.
- **DEP-009**: Recovery readiness requires the content-addressed origin-evidence store, ownership-universe discovery profile, replay projection, and quarantine paths to pass their crash and corruption matrices before any cutover generation can publish.
- **DEP-010**: Physical collection requires the isolated `calibrated_instance` authority root while production remains on legacy/fallback authority. Production cutover requires a later promoted product-and-evidence package built and exact-verified after catalog promotion.

## 5. Files

- **FILE-001**: `docs/research/portable-residency-capability-inventory.json`, `docs/research/portable-residency-capability-matrix.md`, and `docs/research/hatchery-campaign-parameters.md` — source-closed machine contract and generated projections.
- **FILE-002**: `tools/residency_inventory/` and `test/test_residency_capability_inventory_*.py` — inventory source closure, validation, rendering, update transaction, and public-CLI tests.
- **FILE-003**: `tools/generate_residency_contract.py`, `src/cpp/resources/residency_profiles.json`, `src/cpp/include/lemon/residency/generated_contract.h`, and `src/cpp/server/residency/generated_contract.cpp` — deterministic executable contract generation.
- **FILE-004**: `src/cpp/include/lemon/residency/` and `src/cpp/server/residency/` — catalog, types, journal, claims, recovery, providers, planner, coordinator, lifecycle, pressure, configuration, migration, explanations, and evidence-liveness implementation.
- **FILE-005**: `src/cpp/include/lemon/router.h`, `src/cpp/server/router.cpp`, `src/cpp/include/lemon/wrapped_server.h`, and backend wrapper files — lifecycle integration and adapter hooks.
- **FILE-006**: `src/cpp/include/lemon/utils/process_manager.h` and `src/cpp/server/utils/process_manager.cpp` — prepared launch, containment, durable process identity, and verified release.
- **FILE-007**: `src/cpp/include/lemon/server.h`, `src/cpp/server/server.cpp`, `src/cpp/include/lemon/runtime_config.h`, `src/cpp/server/runtime_config.cpp`, `src/cpp/resources/defaults.json`, and configuration files — routes, settings, migration, pins, and grouped startup.
- **FILE-008**: `src/cpp/server/system_info.cpp`, `src/cpp/include/lemon/system_info.h`, `src/cpp/server/model_manager.cpp`, and the stable backend dispatch/selection spine rooted at `src/cpp/include/lemon/backends/{backend_ops,backend_registry}.h` and their implementations — typed observations and exact selected-artifact identity after stable-base reconciliation. Research-only `src/cpp/include/lemon/registry_files.h` is not a stable v11.6.0 implementation seam.
- **FILE-009**: `tools/residency_campaign/` and `test/residency/` — deterministic harness, fake providers, contract, recovery, planner, lifecycle, pressure, API, release, and physical-campaign tests.
- **FILE-010**: `CMakeLists.txt`, `.github/workflows/docs_and_style.yml`, platform packaging files, and `docs/api/schemas/residency/` — generation, CI, packaging, and public schemas.
- **FILE-011**: `src/app/src/renderer/`, `src/web-app/`, and tray/CLI presentation files — server-authored state presentation after the applicable API contract is release-verified.

## 6. Testing

- **TEST-001**: Inventory source-closure tests must reject a missing, wrong-type, or mismatched upstream commit/blob/tree, new unclassified support atom, ambiguous disposition, stale generated projection, omitted transitive artifact-selection helper, or changed/added/removed/relocated backend selector override.
- **TEST-002**: Generated-contract tests must reject byte drift, duplicate or unknown keys, schema-major mismatch, ambiguous selectors, precedence collisions, fallback gaps, and unsupported enum values.
- **TEST-003**: Journal/recovery tests must inject crashes at every authority boundary and prove one-daemon ownership, exact replay, maximum-claim retention, quarantine, verified release, and readiness fencing.
- **TEST-004**: Provider tests must cover topology aliases, incomplete domains, stale/skewed observations, attribution gaps, dependency drift, process identity reuse, and physical-release mismatch.
- **TEST-005**: Planner tests must cover checked arithmetic, manifest completeness, count/capacity independence, stable ordering, protection, simultaneous operations, stale generations, and each registered fallback.
- **TEST-006**: Lifecycle tests must cover load, reload, load-failure retry, explicit/force unload, pin/unpin, resident use, backend death, cancellation, rollback, descendant cleanup, pull/import/adoption, delete, writer fencing, quarantine, and shutdown through named barriers.
- **TEST-007**: Configuration/migration tests must cover strong ETags, stale writers, reset semantics, legacy source conflicts, schema downgrade, saved/runtime pin separation, and all-or-none startup groups.
- **TEST-008**: Pressure tests must cover dwell, critical confirmation, one-action plans, fairness, external demand, evidence failure, release reconciliation, stabilization, and unresolved pressure.
- **TEST-009**: API tests must cover quad-prefix registration, auth, canonical errors, schema conformance, retention, explanation revision equality, and compatibility projectors.
- **TEST-010**: Release tests must verify the inactive `implemented_unverified` transition, exact executable/dependency/catalog/default identities, pre-promotion package contents, `release_verified` delivery transition, the one-way payload → candidate binding/receipt → publication envelope → publicly verified package → per-unit accepted record/receipt graph, singular direct catalog/attestation promotion-index and unit bindings, rejection of current-record receipt backreferences, deterministic unit order, every partial-prefix crash/retry, signatures, primary/backup independence, evidence locators, candidate-versus-accepted witness heads, failure/crash at every append/build/upload/publish/retrieve/verify/adopt boundary, lease expiry, rollback/fork refusal, suspend/resume, restart recovery, and material-closure drift invalidation/requalification without changing capability prematurely.
- **TEST-011**: Hatchery physical tests must satisfy every frozen topology, predictor, ADM, PRE, STA, REC, protection, concurrency, release, liveness, and explanation gate with the prescribed cold-attempt schedule under the isolated campaign authority. Post-promotion release tests must then bind the promoted catalog, primary evidence assets on the same consuming fork release, and independently administered backup identities before production cutover.
- **TEST-012**: Cross-platform release rehearsal must build and test Windows, Linux, and macOS adapters and must prove every unqualified cell remains at its concrete fallback.
- **TEST-013**: Implementation-handoff tests must reconstruct every task-local base-plus-test red fixture in isolation, reproduce its frozen failure signature, verify the identical test patch passes at the green checkpoint, reject self-referential or non-ancestral evidence, and require each phase's complete output/fallback record.

## 7. Risks & Assumptions

- **RISK-001**: Upstream may change support descriptors, artifact selection, process topology, router behavior, or stable implementation seams. Re-run the applicable Phase 0 research-closure and stable-base gates before inheriting any result.
- **RISK-002**: Cross-platform durable publication and process containment semantics differ materially. Keep the platform adapter fail-closed and do not mark a cell release-verified until its real platform tests pass.
- **RISK-003**: A provider may expose incomplete, aliased, stale, or non-causal resource evidence. Preserve unknown/max claims and select refusal or report-only behavior.
- **RISK-004**: Planner or journal scope can become too large for bounded incident and recovery transitions. Use fixed-size hierarchical controls, immutable closure roots, conservative overlays, and incremental materialization.
- **RISK-005**: The pure-offline llama.cpp estimator may diverge from the packaged backend dependency closure. Exact-bind both identities and refuse ordinary admission when faithful sizing cannot stay within its own reserved envelope.
- **RISK-006**: Physical campaign thresholds may prove unsafe or unusably conservative. A failed guard, bound, or utility result requires a newly frozen campaign/predictor revision; it never permits post-observation threshold changes.
- **RISK-007**: A large cutover could obscure upstream compatibility regressions. Preserve old public behavior through generated adapters and land the implementation in the phase order above with fallback-only checkpoints.
- **RISK-008**: Client presentation can drift from server authority. Generate schemas and reason ordering once, and keep clients presentation-only.
- **RISK-009**: A backup controlled by the same administrative authority as the primary cannot satisfy evidence independence. Bind both authority identities and the signed administrative-boundary proof before collection or lease issuance.
- **ASSUMPTION-001**: The `a505bbc7` refresh changes artifact-selection implementation but introduces no new residency support atom, platform predicate, model type, operation, or fallback case.
- **ASSUMPTION-002**: Existing Lemonade subprocess wrappers can expose prepared launch, ownership, observation, and release hooks without changing backend wire protocols.
- **ASSUMPTION-003**: Catalog-internal numeric bounds, warm-retention durations, planning deadlines, backoff caps, storage encodings, and adapter wire shapes may be chosen during implementation when they remain within the accepted semantic contract and receive tests.
- **ASSUMPTION-004**: Backup locator, witness provider/identity, issuer key, exact model artifacts, and physical campaign manifest will be supplied and frozen before collection. They do not block core implementation or harness construction, but TASK-085 fails closed until the complete deployment binding is authorized.
- **ASSUMPTION-005**: Later platform cells remain unsupported or fallback-only until their own exact implementation, release-artifact verification, and evidence gates complete.

## 8. Related Specifications / Further Reading

- [Portable residency capability matrix](../docs/research/portable-residency-capability-matrix.md)
- [Portable residency capability inventory](../docs/research/portable-residency-capability-inventory.json)
- [Hatchery residency validation profile](../docs/research/hatchery-residency-validation-profile.md)
- [Hatchery campaign parameters](../docs/research/hatchery-campaign-parameters.md)
- [Profile-free residency estimation](../docs/research/profile-free-residency-estimation.md)
- [Validation data conventions](../docs/research/upstream-validation-data-conventions.md)
- [Residency architecture map](../docs/agents/architecture-map.md)
- [Residency domain glossary](../CONTEXT.md)
- [Issue 36: Accept the portable residency implementation handoff](https://github.com/nisavid/lemonade/issues/36)
