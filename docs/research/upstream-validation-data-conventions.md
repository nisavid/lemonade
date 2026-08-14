# Upstream validation-data conventions

This note answers the Issue 35 question of how Lemonade should package, distribute,
and extend model/hardware residency profiles. It is based on upstream `main` at
[`23c7ede63b26cf1d083c1e64948607b8a0cd24aa`](https://github.com/lemonade-sdk/lemonade/tree/23c7ede63b26cf1d083c1e64948607b8a0cd24aa)
and the latest published upstream release available on 2026-08-13,
[`v11.5.2`](https://github.com/lemonade-sdk/lemonade/releases/tag/v11.5.2).

## Conclusion

The fork should bundle the small, reviewed, runtime-consumed residency profile
catalog with each Lemonade release. This matches Lemonade's established pattern
for authoritative JSON resources and its stated Auto-Tune direction. Large
sanitized campaign evidence should be separate, content-addressed assets attached
to the same fork release initially, with compact manifests and digests retained
in Git. Runtime-collected observations should remain local and report-only in the
first version; their contribution, curation, and redistribution can be designed
later.

The witness is not a profile-data store, upload service, or distribution layer.
It is a small anti-rollback and liveness authority for the newest accepted catalog
and evidence-manifest identity.

## Conventions Lemonade already has

### Shipped authoritative resources

Upstream keeps small runtime inputs under `src/cpp/resources/`. CMake copies that
tree into the runtime resources directory and packages it with normal installs.
The current examples include:

- `server_models.json`, the built-in model catalog;
- `backend_versions.json`, version, architecture-family, and release-asset
  checksum data;
- `architecture_defaults.json`, compact per-model-family option overlays; and
- `bench_scenarios.json`, the bundled benchmark workload catalog.

See the [resource staging contract](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/CMakeLists.txt#L1271-L1321),
the [Linux package installation](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/CMakeLists.txt#L1873-L1885),
the [architecture defaults](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/src/cpp/resources/architecture_defaults.json#L1-L22),
and the [backend pin and checksum format](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/src/cpp/resources/backend_versions.json#L1-L5).

This convention fits a curated residency catalog. One packaging seam needs
explicit attention: the embeddable archive uses a resource whitelist rather than
copying every file, so a new catalog must be added there as well as to platform
installers. See the [embeddable resource list](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/CMakeLists.txt#L2155-L2185).

### Structured benchmark captures

`lemonade bench` already emits a useful raw-observation scaffold. Its JSON has a
top-level timestamp and hardware profile, then per-model results containing the
recipe, backend, context size, backend arguments, scenarios, timing statistics,
and peak VRAM and RAM. The hardware profile records OS, CPU, RAM, GPU identity,
GPU type/capacity, and installed backend versions.

See the [CLI output contract](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/docs/guide/cli.md#L919-L965),
the [top-level JSON producer](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/src/cpp/cli/bench.cpp#L1118-L1155),
the [per-result shape](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/src/cpp/cli/bench_output.cpp#L167-L249),
and the [hardware vocabulary](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/src/cpp/cli/bench_sysinfo.cpp#L27-L140).

The residency harness should reuse that vocabulary and output style where the
semantics agree. The existing benchmark format is not sufficient by itself for
capacity authority: it lacks the full model/backend/runtime dependency identity,
schema-major contract, canonical byte-unit bounds, time-series pressure evidence,
and attestation chain required by the Issue 35 campaign.

### Planned profile distribution

The upstream Auto-Tune roadmap is the closest direct precedent. It proposes:

- hardware archetypes and a profile JSON schema;
- hand-curated profiles shipped with Lemonade;
- remotely fetched profiles with a versioned local cache;
- bundled defaults as the offline fallback;
- a later `lemonade bench --submit` contribution path; and
- still-later runtime monitoring and local “learn from this run” behavior.

See [Auto-Tune phases 1–5](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/docs/dev/working-groups/auto-tune.md#L25-L55).
These are unchecked roadmap items, not an implemented data service. They are
nevertheless a strong upstream vocabulary and distribution shape to mimic.

### Mutable instance state and telemetry are not catalog authority

Lemonade also has per-instance mutable files and telemetry surfaces, but none is
a versioned runtime-learned profile catalog. `user_models.json` and
`recipe_options.json` live in the Lemonade cache and represent local model
registration and execution preferences; see the [custom-model file contract](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/docs/guide/configuration/custom-models.md#L241-L270).
`GET /v1/stats` reports the last request, while `GET /v1/system-stats` reports a
current lightweight resource sample; see the [statistics endpoints](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/docs/api/lemonade.md#L1200-L1270).
Optional OpenTelemetry export sends bounded request traces to an operator-chosen
collector and includes explicit payload-redaction controls; see the
[telemetry contract](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/docs/guide/telemetry.md#L166-L184).

These are useful measurement inputs or future export mechanisms. Their local,
mutable, or ephemeral nature makes them unsuitable as automatic capacity
authority without the separate campaign validation and promotion process.

### Validation evidence is currently transient

Upstream backend-validation workflows create machine-readable JSON, upload it as
GitHub Actions artifacts for 30 days, summarize it in an update PR, and promote
only the verified backend version pin into `backend_versions.json`. The
[llama.cpp result artifacts](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/.github/workflows/validate_llamacpp.yml#L835-L849)
and [vLLM result artifacts](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/.github/workflows/validate_vllm.yml#L224-L242)
demonstrate this pattern.

The stable-diffusion workflow additionally publishes selected images to a shared
orphan evidence branch, but keeps only the newest three evidence sets and
explicitly permits old PR image links to expire. See its
[normalization and retention contract](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/.github/workflows/validate_sdcpp.yml#L527-L545)
and [evidence-branch publication](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/.github/workflows/validate_sdcpp.yml#L615-L646).

These are useful capture and promotion precedents, but their retention is too
short for an operational residency authority. No committed corpus of benchmark
or validation results exists on upstream `main`.

### Schema, digest, and provenance discipline

The routing schemas establish a reusable format rule: require a schema major,
evolve additively, reject unknown majors, hash canonical JSON in a lock file, and
keep conformance fixtures. See the [schema version and lock contract](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/src/cpp/resources/schemas/README.md#L15-L70)
and [lock test](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/test/test_schema_lock.py#L1-L70).
`backend_versions.json` separately demonstrates exact SHA-256 pins for downloaded
release assets. The model-registry cache records immutable Hugging Face commit
SHAs or a normalized-tree fingerprint when the provider offers no immutable
snapshot identifier; see the [cache provenance contract](https://github.com/lemonade-sdk/lemonade/blob/23c7ede63b26cf1d083c1e64948607b8a0cd24aa/docs/dev/model-registries.md#L32-L50).

These conventions should be reused for profile schema evolution, evidence asset
digests, and source dependency closure.

### Release assets

Upstream `v11.5.2` publishes installers, native packages, and embeddable archives;
it does not publish a standalone benchmark, profile, or validation-data asset.
The fork had no published GitHub releases when checked on 2026-08-13. Attaching
profile manifests and evidence archives to fork releases is compatible with the
existing release mechanism, but it will be a deliberate fork-local convention.

## Recommended Issue 35 split

| Data | Initial home | Runtime authority |
| --- | --- | --- |
| Reviewed residency/capability catalog | Versioned JSON under `src/cpp/resources/`, packaged in every fork release | Offline operational input, bounded by its declared capability state and expiry |
| Catalog schema and canonical lock | Git, beside the resource schema | Defines compatibility and prevents silent schema drift |
| Compact evidence index/manifest | Git and a fork release asset | Binds each accepted cell to exact evidence digests and dependency identity |
| Sanitized raw and normalized campaign evidence | Content-addressed `.tar.zst` assets on the corresponding fork release, with a digest-verified backup | Audit and revalidation input; not loaded wholesale during admission |
| Runtime observations | Local cache or explicit export only | Report-only in the first version; cannot mutate an active bound |
| Witness checkpoint | Separate append-only service/key | Latest-sequence and anti-rollback authority only |

The shipped catalog should use a dedicated name such as
`residency_profiles.json` rather than overloading `server_models.json` or
`architecture_defaults.json`. It should carry a required schema major, catalog
revision, exact cell/dependency identity, modeled or validated state, operational
bounds, evidence-manifest digest, and expiry. A canonical schema hash and
conformance fixtures should protect released meanings.

Putting the large evidence archive next to the fork's normal release assets is a
reasonable first version and matches the operator's desired discovery path. The
archive should remain a separate asset rather than being embedded in every MSI,
package, or embeddable archive: the server needs the compact reviewed catalog,
while raw traces increase download size and may require narrower disclosure. If
campaign cadence later diverges from product releases, a dedicated evidence-only
release series can preserve the same manifest and digest contract without
changing runtime semantics.

Runtime-collected extensions are intentionally deferred. The first version may
export observations in a bench-like JSON envelope, but it should neither upload
them automatically nor merge them into the accepted catalog. A future iteration
can design submission, deduplication, redaction, outlier handling, review,
promotion, and redistribution, following the Auto-Tune roadmap's separation
between capture, curation, and application.

## Witness boundary

The witness stores only compact authority metadata, for example:

```text
sequence
catalog digest
evidence-manifest digest
previous checkpoint digest
signing-key identity
checkpoint time
```

It proves that Lemonade is using the newest accepted catalog/evidence checkpoint
and that the referenced evidence remains available. It does not receive bulk
measurements, build profiles from local runs, decide whether a run is trustworthy,
or redistribute runtime-learned data. Those functions belong to the deferred
contribution and curation system.
