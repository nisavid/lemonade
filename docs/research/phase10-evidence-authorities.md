# Phase 10 evidence-backup and witness authorities

Status: research for Issue
[#89](https://github.com/nisavid/lemonade/issues/89), current on
2026-08-18. No provider has been selected, authorized, or provisioned.

## Finding

No evidence authority is selected or qualified. The bounded backup paper screen
finds documented paths through the required retention, conditional-write, public
read, and administrative-boundary gates for Azure Blob Immutable Storage.
Google Cloud Storage Bucket Lock and Amazon S3 Object Lock in compliance mode
meet the object-level gates but remain conditional because their documented
project or account deletion paths do not preserve retained objects and their
public locators. Each candidate still requires the same destructive
qualification. Backblaze B2 Object Lock lacks reviewed first-party proof of an
exact create-only upload primitive, and Cloudflare R2's documented lock rules
remain removable.

No surveyed witness satisfies the complete Phase 10 contract as documented.
Tessera plus C2SP witnesses can prove append-only log growth, but a dedicated
personality must atomically enforce Lemonade's application tip. Azure
Confidential Ledger's stable append API has no expected-tip field, while its
preview UDF surfaces provide a plausible in-ledger CAS path that still has
enforcement, receipt-binding, governance, and availability gaps. Rekor provides
public transparency proofs but does not enforce a Lemonade namespace's
application chain.

Issue #90 can therefore compare unselected options without inheriting a
provider recommendation:

- apply the same disposable-resource qualification to every backup candidate
  that passes the paper screen;
- preserve B2's conditional-write gap and R2's retention-authority gap rather
  than inferring missing guarantees;
- compare a Tessera/C2SP application personality with the Confidential Ledger
  preview UDF path as distinct witness operating models; and
- treat Rekor as a possible secondary transparency mirror, not as the sole
  conditional-append authority.

This conclusion applies the accepted distinction between bulk evidence storage
and the compact
[residency evidence witness](https://github.com/nisavid/lemonade/blob/6ab2880b6c2a6810d317bb59c808f73ecc8ec774/CONTEXT.md).
It also preserves the Phase 10 rule that review cannot replace independent
physical evidence or the external authorities required by
[DEP-005 and DEP-007](https://github.com/nisavid/lemonade/blob/6ab2880b6c2a6810d317bb59c808f73ecc8ec774/plan/architecture-portable-residency-1.md#4-dependencies).

## Contract used for comparison

The
[campaign deployment binding](https://github.com/nisavid/lemonade/blob/6ab2880b6c2a6810d317bb59c808f73ecc8ec774/plan/architecture-portable-residency-1.md#campaign-deployment-binding)
requires separate backup and witness authority roles.

The backup authority must provide:

- an administrative boundary independent from GitHub and identified by account,
  tenant, provider, principals, and recovery controls;
- immutable evidence objects addressed by exact locator, SHA-256 digest, and
  size;
- create-only or conditional writes so a content-addressed key cannot silently
  change;
- a durable retention class and publicly retrievable exact bytes; and
- separate writer, reader, retention-administrator, and recovery access.

The witness authority must additionally provide one operation with this exact
state transition:

```text
append(namespace, expected_prior_tip, record_digest)
    -> either conflict(current_tip)
    -> or pending(current_tip, operation_key)
    -> or signed_receipt(namespace, new_tip, expected_prior_tip, record_digest)
```

The operation key and idempotency key are the same tuple,
`(namespace, expected_prior_tip, record_digest)`. Before the application CAS,
ordinary concurrency rules apply. After that CAS records a pending operation,
every later append in the same namespace must return the deterministic
`pending(current_tip, operation_key)` response without changing state until the
original keyed operation reaches a stored receipt. Only then may an ordinary
successor append against `current_tip` enter the CAS. After receipt persistence,
every retry of the original key must return the stored original receipt. Reusing
one `(namespace, expected_prior_tip)` with a different `record_digest` must then
return a conflict and must not append another record.

The expected tip comparison, record append, and new-tip persistence must be one
atomic operation. A successful retry after a lost response must return the same
committed identity rather than append a duplicate. The receipt or checkpoint
must verify offline or through a public read surface using a pinned verification
key. Public availability, log append-only consistency, and application-level CAS
are separate properties.

The signed receipt payload must bind `namespace` as well as `new_tip`,
`expected_prior_tip`, and `record_digest`. If a transport or publication
envelope also carries a namespace, a verifier must require exact equality with
the signed namespace and reject a mismatch before accepting the receipt or
changing state.

An object store need not sign a witness receipt. Its job is to preserve and
serve exact bytes. Conversely, a transparency log can prove that its Merkle tree
only grew while still accepting two records that name the same application
predecessor. The latter does not satisfy the Phase 10 acceptance chain.

## Evidence-backup candidates

### Roster method and boundary

This is a bounded primary-source paper screen, not an exhaustive storage-market
survey. The roster covers three general-purpose cloud object stores and two
cost-focused object stores already in the research set. Each is screened against
locked retention, conditional creation, public exact retrieval, and separable
administration; a missing gate is recorded rather than inferred. Contract-only
archival services and additional vendors are outside this ticket. They can enter
a later comparison only through the same four-gate evidence record and
qualification checks.

The witness roster is bounded in the same way: one deployable transparency-log
stack, one managed confidential ledger, and one public transparency service.
These represent distinct authority and operating models; they are not a claim
that every witness product has been surveyed.

| Candidate | Immutable retention | Conditional write | Public exact retrieval | Administrative and operational boundary | Research disposition |
| --- | --- | --- | --- | --- | --- |
| Google Cloud Storage Bucket Lock | Strong inside an active project after irreversible policy lock; project deletion remains possible after lien removal | Strong generation preconditions | Supported while the project and account remain active | Separate Google Cloud project, IAM principals, and recovery identity are possible | Conditional: project and account deletion |
| Amazon S3 Object Lock, compliance mode | Strong per protected object version inside an active account; account deletion is a documented bypass | Strong `If-None-Match` and `If-Match` controls | Supported directly or through CloudFront while the account remains active | Separate AWS account, roles, and recovery identity are possible | Conditional: account deletion |
| Azure Blob Immutable Storage | Strong under a locked time-based policy | Strong `If-None-Match` and `If-Match` controls | Supported for an exact version when anonymous access is enabled | Separate Azure subscription, tenant principals, and recovery identity are possible | Paper-screen pass; unqualified |
| Backblaze B2 Object Lock, compliance mode | Strong for 1–3,000 days per documented file retention | Not established by reviewed docs | Supported by public buckets | Bucket/prefix-scoped application keys are available | Paper screen incomplete: conditional write |
| Cloudflare R2 Bucket Lock | Lock prevents overwrite/delete while the rule applies, but rules are removable | Strong ETag conditions | Supported by custom domain or `r2.dev` | Bucket-scoped tokens are available | Paper screen incomplete: retention authority |

### Google Cloud Storage Bucket Lock

Facts:

- A bucket retention policy prevents deleting or replacing objects before their
  retention age. Locking the policy is irreversible: the policy cannot be
  removed or shortened, the bucket cannot be deleted while an object remains
  inside retention, and Google applies a project-deletion lien. Existing and new
  objects are covered. Object metadata remains editable, so metadata is not an
  integrity authority. See the official
  [Bucket Lock contract](https://docs.cloud.google.com/storage/docs/bucket-lock).
- The automatically applied project lien is a separate, removable control.
  Removing it requires `resourcemanager.projects.updateLiens`, which is included
  in `roles/owner` and `roles/resourcemanager.lienModifier`; project deletion
  requires removing the lien first. See the official
  [project-lien contract](https://docs.cloud.google.com/resource-manager/docs/project-liens).
- Cloud Storage write preconditions support exact object generations.
  `ifGenerationMatch=0` makes an upload succeed only when there is no live object
  at that name; a mismatched generation fails with `412 Precondition Failed`.
  Google recommends generation and metageneration preconditions over ETags for
  stronger cross-API coverage. See
  [request preconditions](https://docs.cloud.google.com/storage/docs/request-preconditions).
- Cloud Storage is designed for at least eleven nines annual durability across
  storage classes and locations. Dual- and multi-region buckets remain available
  during a region-wide outage without changing paths. See
  [availability and durability](https://docs.cloud.google.com/storage/docs/availability-durability).
- Anonymous read is possible through an `allUsers` read grant when public access
  prevention does not apply. Uniform bucket-level access can keep authorization
  at one IAM surface. See the
  [public-read ACL contract](https://docs.cloud.google.com/storage/docs/access-control/lists)
  and
  [uniform bucket-level access](https://docs.cloud.google.com/storage/docs/uniform-bucket-level-access).
- IAM can give the upload path only `Storage Object Creator`; external workloads
  can use short-lived Workload Identity Federation credentials rather than a
  service-account key. A GitHub-issued identity would weaken the claimed
  administrative independence if it were the only write or recovery authority.
  See [Cloud Storage IAM](https://docs.cloud.google.com/storage/docs/access-control/iam)
  and [workload identities](https://docs.cloud.google.com/iam/docs/workload-identities).
- Pricing varies by location and class. The first-party
  [pricing table](https://cloud.google.com/storage/pricing) separately charges
  stored bytes, operations, retrieval, replication, and network transfer.

Fit and gaps:

- The first-party material documents irreversible bucket retention and a native
  create-only upload precondition, but the explicit project and account deletion
  paths prevent a full retention pass. GCS remains conditional unless an
  external control preserves both the retained object and its public locator.
- A locator must bind the object generation, SHA-256, and size. A mutable object
  name without a generation is insufficient even when replacement is delayed.
- The independence proof must establish a separate Google Cloud project and
  recovery authority, not merely a different hostname reached by GitHub Actions.
- A prototype must confirm that the permanent public locator exposes the exact
  generation. It must exercise lien removal and attempted project deletion while
  an object remains under retention, and prove that account, project, IAM, and
  billing failure modes do not bypass the required retention period.

### Amazon S3 Object Lock

Facts:

- S3 Object Lock uses a WORM model and requires versioning. In compliance mode,
  a protected version cannot be overwritten or deleted by any user, including
  the AWS account root user; its mode cannot change and its retention cannot be
  shortened. AWS documents account deletion as the only way to delete it early.
  A simple delete can still add a delete marker and a same-name write can create
  a newer version. See
  [How S3 Object Lock works](https://docs.aws.amazon.com/AmazonS3/latest/userguide/object-lock.html).
- S3 can enforce `If-None-Match` or `If-Match` headers on uploads with bucket
  policy conditions. See
  [enforcing conditional writes](https://docs.aws.amazon.com/AmazonS3/latest/userguide/conditional-writes-enforce.html).
- S3 can validate and retain a SHA-256 checksum for single- or multipart uploads.
  Multipart ETags are not full-object digests. See
  [upload integrity checks](https://docs.aws.amazon.com/AmazonS3/latest/userguide/checking-object-integrity-upload.html).
- S3 Standard is designed for eleven nines annual durability, 99.99% annual
  availability, and storage across at least three Availability Zones. See
  [S3 storage durability](https://docs.aws.amazon.com/AmazonS3/latest/userguide/DataDurability.html).
- A private bucket can be served publicly through CloudFront while origin access
  control limits direct bucket reads. An exact S3 version is selected with the
  `versionId` query parameter and requires `s3:GetObjectVersion`. A CloudFront
  locator that retains the query form must forward `versionId` to S3 and, when
  caching is enabled, include it in the cache key. The alternatives are a
  version-specific immutable path mapping or disabled caching with `versionId`
  still forwarded. See the
  [S3 object-version retrieval contract](https://docs.aws.amazon.com/AmazonS3/latest/userguide/RetrievingObjectVersions.html),
  [S3 `GetObject`](https://docs.aws.amazon.com/AmazonS3/latest/API/API_GetObject.html),
  [CloudFront cache-key contract](https://docs.aws.amazon.com/AmazonCloudFront/latest/DeveloperGuide/cache-key-understand-cache-policy.html),
  and
  [CloudFront S3 origin contract](https://docs.aws.amazon.com/AmazonCloudFront/latest/DeveloperGuide/private-content-restricting-access-to-s3.html).
- AWS recommends least-privilege IAM roles and temporary credentials instead of
  stored long-lived access keys. See the
  [S3 security practices](https://docs.aws.amazon.com/AmazonS3/latest/userguide/security-best-practices.html).
- The first-party [S3 pricing page](https://aws.amazon.com/s3/pricing/) varies by
  region, storage class, request type, retrieval, replication, and egress.

Fit and gaps:

- Compliance mode plus create-only conditional writes is a strong object-level
  backup fit, but AWS documents account deletion as the way to delete a protected
  version early. S3 remains conditional unless an enforceable external control
  preserves both the retained bytes and public locator through the retention
  period.
- The deployment binding must name the exact S3 version ID as well as the key,
  SHA-256, and size. Object Lock protects a version; it does not stop a newer
  version or delete marker from becoming current.
- Exact-version CloudFront cache behavior, KMS/key policy if used, billing
  recovery, and root recovery controls need an explicit operator risk decision;
  that decision cannot waive the account-deletion qualification gate.
- The independence proof must bind a separate AWS account and non-GitHub recovery
  principals. A GitHub OIDC role may be a narrow uploader, but it cannot be the
  sole administrator or recovery path.

### Azure Blob Immutable Storage

Facts:

- Azure Blob Immutable Storage provides container- and version-level WORM.
  Microsoft states that a locked time-based policy prevents modification or
  deletion even by users with account administrative privileges. A policy can
  retain data for 1 to 146,000 days; after locking, it cannot be deleted or
  shortened. An account or container protected by version-level WORM can be
  deleted only after it is empty. See the
  [immutable-storage contract](https://learn.microsoft.com/en-us/azure/storage/blobs/immutable-storage-overview).
- Blob write operations support `If-None-Match: *`, which succeeds only if the
  resource does not exist, and `If-Match` against an ETag. A failed condition
  returns `412 Precondition Failed`. See
  [Blob conditional headers](https://learn.microsoft.com/en-us/rest/api/storageservices/specifying-conditional-headers-for-blob-service-operations).
- A blob version is retrievable through `Get Blob` with its opaque `versionid`
  query parameter. Anonymous read requires both account-level permission for
  public access and a container access level that permits blob reads. See
  [Get Blob](https://learn.microsoft.com/en-us/rest/api/storageservices/get-blob)
  and
  [anonymous read access](https://learn.microsoft.com/en-us/azure/storage/blobs/anonymous-read-access-configure).
- Microsoft Entra authorization and Azure RBAC can separate data readers,
  contributors, owners, resource administrators, and recovery principals. A
  management role that can list storage-account keys can still acquire Shared
  Key data access, so the independence proof must cover that path. See
  [Blob authorization with Microsoft Entra ID](https://learn.microsoft.com/en-us/azure/storage/blobs/authorize-access-azure-active-directory).
- Immutable data has no extra capacity charge, but version-level WORM can add
  stored versions and policy changes incur operation charges. Storage, request,
  redundancy, retrieval, and transfer prices vary by region and tier. See the
  [immutable-storage pricing notes](https://learn.microsoft.com/en-us/azure/storage/blobs/immutable-storage-overview#pricing)
  and [Blob Storage pricing](https://azure.microsoft.com/en-us/pricing/details/storage/blobs/).

Fit and gaps:

- Locked WORM, native create-only conditions, and exact-version retrieval meet
  the documented paper screen without ranking Azure ahead of the other passes.
- The binding must name the version ID, ETag, SHA-256, and size. A blob name
  without `versionid` can resolve to a newer current version.
- A destructive qualification must prove the policy is locked at the intended
  scope, `If-None-Match: *` cannot replace the bound version, and the anonymous
  URL returns that exact version before and after a current-version change.
- The administrative proof must bind a separate subscription and recovery
  authority, account for Shared Key and `listKeys` access, and test account
  deletion, billing failure, regional failover, public-access policy, and
  credential recovery.

### Backblaze B2 Object Lock

Facts:

- Enabling B2 Object Lock on a bucket is irreversible. Compliance-mode file
  retention cannot be removed by any user, although an authorized client can
  extend it. The documented retention range is one to 3,000 days; legal hold is
  indefinite but separately removable. Object Lock has no feature surcharge.
  See [B2 Object Lock](https://www.backblaze.com/docs/cloud-storage-object-lock).
- Public buckets expose credential-free friendly, S3, and native URLs while
  remaining non-public for writes. File IDs identify versions. See the
  [developer quickstart](https://www.backblaze.com/docs/cloud-storage-developer-quick-start-guide).
- Standard application keys can be restricted to a bucket, prefix, capabilities,
  and expiration. The master key has complete access and no expiration. See
  [application keys](https://www.backblaze.com/docs/cloud-storage-application-keys)
  and
  [application-key capabilities](https://www.backblaze.com/docs/cloud-storage-application-key-capabilities).
- The native upload API verifies SHA-1. The Phase 10 SHA-256 remains an
  application-level digest that must be stored and independently reverified.
  See the
  [native upload checksum contract](https://www.backblaze.com/docs/cloud-storage-upload-files-with-the-native-api).
- First-party pricing currently starts at $6.95/TB-month, with free egress up to
  three times average monthly storage and paid excess egress. See
  [B2 pricing](https://www.backblaze.com/cloud-storage/pricing).

Fit and gaps:

- B2 offers strong compliance retention and a simple anonymous read surface at
  low expected evidence volumes.
- The reviewed native and S3-compatible documentation did not establish an
  upload precondition equivalent to create-if-absent or compare-and-swap. Unique
  SHA-256 path names reduce collision risk but do not prove that the provider
  rejects a second write to the same logical authority name.
- The 3,000-day retention ceiling, account deletion behavior, whole-file SHA-256
  verification, stable version locator, and conditional-write behavior require
  destructive qualification before B2 can pass the paper screen.

### Cloudflare R2 Bucket Lock

Facts:

- R2 lock rules can prevent deletion and overwrite for a duration, until a date,
  or indefinitely. The same documentation also provides dashboard, Wrangler,
  and API operations to remove lock rules. See
  [R2 bucket locks](https://developers.cloudflare.com/r2/buckets/bucket-locks/).
- R2 `put()` supports ETag and time conditions, and a failed condition returns no
  object. Its S3-compatible upload path also supports conditional headers. See
  the
  [Workers API conditional operations](https://developers.cloudflare.com/r2/api/workers/workers-api-reference/)
  and [S3 API compatibility](https://developers.cloudflare.com/r2/api/s3/api/).
- Public buckets can use a custom domain or a Cloudflare-managed `r2.dev`
  subdomain; the latter is documented as non-production. See
  [public buckets](https://developers.cloudflare.com/r2/buckets/public-buckets/).
- R2 is designed for eleven nines annual durability and has a 99.9% availability
  SLA. Cloudflare explicitly distinguishes durability from protection against
  intentional deletion. See [R2 durability](https://developers.cloudflare.com/r2/reference/durability/).
- Account or user tokens can be narrowed to object read/write on selected
  buckets. See [R2 authentication](https://developers.cloudflare.com/r2/api/tokens/).
- Standard storage is currently $0.015/GB-month, includes a 10 GB-month free
  tier, and has no Internet egress charge. See
  [R2 pricing](https://developers.cloudflare.com/r2/pricing/).

Fit and gaps:

- Conditional writes, checksums, public delivery, and cost are attractive.
- The official docs do not claim that a lock rule is irreversible or that a
  privileged account administrator cannot release objects early. Because rule
  removal is documented, treating R2 as compliance WORM would be an unsupported
  inference. It does not pass the backup paper screen unless provider evidence
  proves the required administrator-resistant retention.

## Anti-rollback witness candidates

| Candidate | Exact application prior-tip CAS | Signed proof | Public verification | Managed availability | Research disposition |
| --- | --- | --- | --- | --- | --- |
| Tessera plus C2SP witnesses | Missing from `Add`; must be added by a personality/sequencer | Signed checkpoint plus timestamped witness cosignature | Static log and witness checkpoints are publicly verifiable | Deployment-specific; no general-purpose provider is selected | Deployable comparison candidate; unqualified |
| Azure Confidential Ledger | Stable append lacks it; preview UDFs provide a plausible in-ledger CAS path | Merkle write receipt signed by the ledger service | Offline receipt verification is supported; data-plane reads require authorization | Azure-managed, zone-resilient, region-paired | Managed comparison candidate; unqualified |
| Sigstore Rekor | No Lemonade namespace prior-tip CAS | Inclusion proof and signed checkpoint/tree head | Public log and verification tooling | Public instance has 99.5% SLO | Secondary transparency mirror only |

### Tessera with C2SP witnesses

Facts:

- Tessera is a Go library for building tile-based transparency logs, not a hosted
  witness provider. `Appender.Add(entry)` returns an assigned index. Integration
  and checkpoint publication happen in the background. A publication awaiter
  can block until a public checkpoint commits to the entry. Tessera can require
  a witness policy before publishing a checkpoint. See the Tessera source and
  integration contract reviewed at commit
  [`8e9e6b45722a1a8534a0ebb3b2218d874c3c3f47`](https://github.com/transparency-dev/tessera/blob/8e9e6b45722a1a8534a0ebb3b2218d874c3c3f47/README.md).
- The C2SP witness protocol accepts an old tree size, a consistency proof, and a
  log-signed new checkpoint. A witness must compare the supplied old size with
  the latest size it cosigned, return `409 Conflict` on mismatch, atomically
  persist the newer checkpoint before responding, and return a timestamped
  cosignature. This is a real conditional update for the witness's per-log
  checkpoint state. See the stable
  [C2SP transparency-log witness protocol](https://c2sp.org/tlog-witness).
- The reference witness implementation tracks one checkpoint per log, verifies
  append-only evolution, implements the C2SP HTTP API, and can run as
  OmniWitness. It is software that an operator deploys; the reviewed material
  does not offer a generic hosted custom-log account, SLO, retention commitment,
  or price. See the transparency-dev witness implementation reviewed at commit
  [`086353d83f6c72da9a90a0ed381927e114f6b212`](https://github.com/transparency-dev/witness/blob/086353d83f6c72da9a90a0ed381927e114f6b212/README.md).
- A verifier policy can require a log signature plus a quorum of named witness
  signatures, enabling offline proof policy. See
  [C2SP transparency-log trust policy](https://c2sp.org/tlog-policy).

Exact prior-tip test:

Tessera and C2SP protect the Merkle tree's append-only evolution. They do not by
themselves protect Lemonade's `prior accepted authority tip`. Two callers can
submit records `A(prev=X)` and `B(prev=X)` to `Appender.Add`; both can become
different sequential leaves in one valid tree, and witnesses can correctly
cosign the enlarged checkpoint. The C2SP `old` field refers to the witness's
prior *tree size*, not the prior tip embedded in either application record.

Prototype shape:

1. A dedicated personality exposes `append(namespace, expected_tip, record)`.
2. One transactional sequencer compares `expected_tip` against the namespace
   state, stores the immutable record, and advances the namespace tip exactly
   once.
3. The resulting leaf enters Tessera; checkpoint publication waits for an
   independently administered C2SP witness quorum.
4. The response is a detached receipt binding the namespace, application
   predecessor, record digest, assigned leaf, inclusion proof, log checkpoint,
   and witness cosignatures.
5. Public static tiles/checkpoints and witness monitoring endpoints permit
   offline and independent verification.

This is a technically plausible option, but it is a new service. Qualification
requires named log and witness operators, separate control planes and keys,
deployment and recovery ownership, an availability target, retention, cost, and
security review. A self-operated witness under the same account as the log does
not prove the required administrative independence.

### Azure Confidential Ledger

Facts:

- Azure Confidential Ledger is a customer-managed, append-only ledger running
  across three or more enclave-backed instances. Transactions have Merkle
  receipts, nodes span availability zones, ledger files are replicated to a
  paired region, and the service can self-heal or recover. A control-plane
  administrator can hard-delete the ledger, after which its data is not
  recoverable. See the
  [Azure Confidential Ledger overview](https://learn.microsoft.com/en-us/azure/confidential-ledger/overview).
- A committed write receipt is a Merkle proof that can be independently checked
  using the documented verification algorithm and service endorsement chain.
  See
  [write receipts](https://learn.microsoft.com/en-us/azure/confidential-ledger/write-transaction-receipts)
  and
  [receipt verification](https://learn.microsoft.com/en-us/azure/confidential-ledger/verify-write-transaction-receipts).
- Simple UDFs execute custom JavaScript inside the ledger trust boundary. They
  can read and write custom CCF application tables and inspect the collection
  and contents of a hooked transaction. UDFs and hooks execute on the primary
  replica with strong consistency and in one atomic transaction; an exception
  rolls back the whole operation. The built-in ledger-entry tables are
  read-only. See
  [simple UDFs](https://learn.microsoft.com/en-us/azure/confidential-ledger/user-defined-functions).
- Simple UDFs are gated preview functionality under API version
  `2024-12-09-preview`. Transaction hooks must be named by each caller and do
  not run automatically for every write. An administrator can create, replace,
  or delete UDF code. UDF code must be self-contained, hook execution is limited
  to five seconds, and one write can name at most five hooks total across pre-
  and post-hooks. Simple and advanced UDFs are mutually exclusive, and switching
  modes deletes the current UDF definitions. See the
  [UDF overview](https://learn.microsoft.com/en-us/azure/confidential-ledger/server-side-programming).
- Advanced UDFs run application code in the same trusted execution environment
  and can expose custom endpoints with custom RBAC. They are separately gated
  preview functionality under API version `2024-08-22-preview`, and only ledger
  administrators can deploy the application bundle or manage its custom RBAC.
  See
  [advanced UDFs](https://learn.microsoft.com/en-us/azure/confidential-ledger/user-defined-endpoints).
- Data-plane access uses Microsoft Entra tokens or certificates and ledger-local
  roles. A `public` ledger means plaintext storage, not anonymous append or read.
  See
  [Microsoft Entra authentication](https://learn.microsoft.com/en-us/azure/confidential-ledger/authentication-azure-ad).
- Pricing is hourly per standard ledger plus storage after the included amount;
  the first-party page requires a calculator or quote for current rates and says
  the service is available in limited regions. See
  [Azure Confidential Ledger pricing](https://azure.microsoft.com/en-us/pricing/details/confidential-ledger/).

Exact prior-tip test:

The stable create-entry request is `POST /app/transactions` with optional
`collectionId`; its body contains `contents` and no expected-tip, ETag,
transaction precondition, or conflict response. See the
[Create Ledger Entry REST contract](https://learn.microsoft.com/en-us/rest/api/data-plane/confidentialledger/create-ledger-entry/create-ledger-entry?view=rest-data-plane-confidentialledger-2022-05-13).

The preview changes the architectural result without qualifying the service. A
standalone simple UDF can plausibly accept a namespace, expected tip, request
identity, and record digest; compare the expected tip with a custom application
table; return the prior committed identity for an idempotent retry; and atomically
write the record, new tip, and request mapping. The documented primary-replica,
strong-consistency, and rollback guarantees make this an in-ledger CAS path, not
an external database wrapper. The preview
[execute-UDF REST contract](https://learn.microsoft.com/en-us/rest/api/data-plane/confidentialledger/execute-user-defined-function/execute-user-defined-function?view=rest-data-plane-confidentialledger-2024-12-09-preview)
does not itself establish the Phase 10 receipt and conflict response shape.

A pre-hook on `POST /app/transactions` is insufficient as an enforcement
boundary because callers choose whether to invoke it. A simple-UDF design must
make its custom tables the sole authoritative acceptance state and prove that
ordinary ledger writes cannot mutate or be mistaken for that state. The other
route is an advanced custom endpoint whose RBAC makes it the sole acceptance
path. The advanced path is also preview, and its first-party overview does not
by itself prove the exact concurrency, atomicity, retry, and receipt semantics
required here.

Either UDF path must demonstrate that a committed execution exposes a stable
transaction identity and that the independently verified receipt binds the
namespace, custom-table record, predecessor, digest, and idempotency result. It
must also prove conflict behavior under concurrency, fail-closed UDF upgrades,
pinned code identity, administrator and service-certificate governance, and
recovery after a lost response. Preview access and terms, limited regions and
pricing, authenticated reads, and control-plane hard deletion remain
availability and retention gaps. Azure therefore remains an unqualified
managed comparison candidate; the documentation does not support selecting or
provisioning it.

### Sigstore Rekor

Facts:

- Rekor is a public, immutable, tamper-resistant software-supply-chain
  transparency log. The public instance has a 99.5% availability SLO and an
  on-call team. Auditors can monitor append-only consistency. See the
  [Rekor overview](https://docs.sigstore.dev/logging/overview/).
- Rekor v1 clients can upload signed metadata, retrieve entries, verify inclusion
  proofs, and verify the log signature on a signed tree head. See the
  [Rekor CLI contract](https://docs.sigstore.dev/logging/cli/).
- Rekor v2 accepts a signed artifact digest, waits for a published checkpoint
  that includes the entry, and returns an inclusion proof and signed C2SP
  checkpoint. It intentionally does not store the full attestation. Community
  log URLs change at shard boundaries. See the Rekor v2
  [client contract](https://github.com/sigstore/rekor-tiles/blob/f912f8ef679ca64f41f6899702532595c892421a/CLIENTS.md)
  and
  [source](https://github.com/sigstore/rekor-tiles/tree/f912f8ef679ca64f41f6899702532595c892421a),
  both reviewed at commit `f912f8ef679ca64f41f6899702532595c892421a`.
- The Rekor v2 client design says synchronous third-party witnessing is planned;
  initial launch checkpoints are not witnessed while a public witness network
  is pending. This is a current trust and rollout gap, not a promised available
  authority.

Exact prior-tip test:

Rekor's upload body binds an artifact digest and signature. It does not accept
the caller's prior Lemonade acceptance tip as a server-enforced condition.
Multiple signed records naming one predecessor can all enter the log without
violating Rekor's append-only property. Rekor is consequently useful as a public
secondary timestamp/transparency mirror, but it cannot be the sole Phase 10
conditional-append witness.

## Administrative-independence proof

A provider name is not evidence of administrative independence. Before freezing
`hatchery_deployment_binding/v1`, the signed independence proof should bind:

- provider and service; tenant, account, subscription, project, bucket, ledger,
  log origin, and witness identities;
- immutable policy and retention-class identities, including effective time and
  any account-deletion exception;
- separate principals for upload/append, public read, retention administration,
  key use, monitoring, billing recovery, and break-glass recovery;
- the verification-key digest and where the private witness key is generated,
  stored, used, rotated, and recovered;
- every GitHub trust path. GitHub may trigger a narrowly scoped upload, but it
  must not be the only administrator, key custodian, billing owner, witness
  operator, or recovery authority;
- the exact public locator namespace and independent DNS/control-plane owner;
- provider terms, region, availability target, durability claim, data residency,
  retention, deletion, suspension, and account-recovery boundaries; and
- signed results of the destructive and recovery checks below.

The evidence backup and witness are distinct authority roles, but they need not
use different provider companies. One provider may operate both only when the
signed proof binds explicit backup-administration and witness-append boundaries:
the backup writer or retention administrator cannot exercise witness append,
administration, or key custody, and the witness operator cannot change retained
backup bytes. Shared provider, account, credential-root, billing, or recovery
dependencies must be recorded as correlated failure rather than claimed as
independence. Qualification must exercise each role's authorized credentials and
simulate role or account suspension in turn, proving that the other role's
retained evidence or append state cannot be changed. The primary GitHub Release,
backup authority, and witness authority remain three independently identifiable
authority identities.

## Required qualification checks

### Backup checks

Run these against disposable resources before any production binding:

1. Lock the smallest safe test retention and record the immutable policy,
   account, project, region, and effective time.
2. Upload one known object under its SHA-256 path using the provider's strongest
   create-only precondition. Bind the returned version/generation, provider
   checksum, SHA-256, and size.
3. Retry the same bytes and then different bytes at the same key. The contract
   must either be idempotent or reject the write; it must never silently change
   the bound object.
4. Attempt delete, overwrite, metadata mutation, retention shortening, policy
   removal, bucket deletion, and project/account deletion with the writer,
   administrator, and root-equivalent principals. For GCS, remove the automatic
   project lien with a principal holding `resourcemanager.projects.updateLiens`,
   then attempt project deletion while the retained object still exists. Record
   every response. A blocked attempt is insufficient when a removable lien is
   the blocker; deletion or loss of public retrieval before retention expiry
   fails the candidate unless a separate enforceable control preserves both.
5. Retrieve the exact bound version anonymously from a stable production URL,
   then verify size and SHA-256. Repeat from a second network and after cache
   purge. For CloudFront-backed S3, grant `s3:GetObjectVersion` to the
   `cloudfront.amazonaws.com` service principal, scoped to the distribution;
   create two distinct version IDs at one key with known, different payloads;
   fetch both exact-version public locators without a purge; purge the cache and
   fetch both again. Verify the exact bytes, recorded size, and SHA-256 of every
   response. Any cache-key collision or origin access denial fails the
   candidate.
6. Revoke the upload credential, prove public reads continue, and recover access
   through the separately held break-glass path.
7. Exercise billing suspension and provider-region outage procedures or obtain a
   first-party commitment describing them.
8. Confirm that logs and receipts reveal no unsanitized campaign data or secret.

### Witness checks

Any witness prototype must pass these tests at its public API:

1. Submit two concurrent records with the same `namespace` and
   `expected_prior_tip` but different `record_digest` values. Exactly one
   application CAS succeeds. A request observed while that operation is pending
   follows check 5; after receipt persistence, retrying the competing digest
   returns a conflict naming the committed tip. Exactly one record and leaf
   exist.
2. Submit the identical full idempotency key concurrently. Exactly one record is
   appended. While the operation is pending, every other response follows check
   5; after receipt persistence, every retry returns the stored original receipt.
3. Lose the successful response after durable commit, then retry. The retry
   uses the same `(namespace, expected_prior_tip, record_digest)` idempotency key,
   returns the stored original receipt, and does not append a second record.
   Retrying the same predecessor with a different digest returns a conflict and
   also leaves the log unchanged.
4. Verify that the signed receipt binds `namespace`. Reject any receipt whose
   outer-envelope namespace differs from its signed namespace, without changing
   state.
5. Once application CAS records a pending operation, submit later appends in
   that namespace using the identical operation key, the same predecessor with
   a different digest, and the new current tip as an ordinary successor. Every
   request must return the same deterministic
   `pending(current_tip, operation_key)`, emit no leaf, and leave state unchanged
   until the original keyed operation reaches a stored receipt. Then prove that
   the original key returns that receipt and an ordinary successor enters the
   normal CAS path.
6. Reject a stale, unknown, malformed, unauthorized, cross-namespace, or
   digest-mismatched predecessor without changing state.
7. Crash at every boundary between validation, application CAS, log sequencing,
   checkpoint publication, witness cosigning, receipt persistence, and response.
   The durable state and restart result must match the recovery oracle below.
8. Verify the detached receipt, inclusion proof, log checkpoint, witness quorum,
   key identity, and application predecessor offline from pinned trust roots.
9. Retrieve current and historical public checkpoints without a write secret;
   detect stale service, split view, rollback, missing liveness, and key
   rotation.
10. Remove one witness, log node, region, billing principal, and operator. The
   documented quorum and recovery policy must either remain healthy or fail
   closed without issuing a receipt.
11. Prove that log, witness, issuer, backup, and GitHub administrators cannot
   impersonate one another through shared SSO, secrets, cloud account, or
   recovery contacts.

Crash recovery oracle:

| Crash boundary | Durable authority state | Required restart and retry result | Later append result in the namespace |
| --- | --- | --- | --- |
| After validation, before application CAS | Prior tip only; no keyed operation or receipt | Revalidate from the prior tip; either one later append commits or a competing tip conflicts. | Ordinary concurrency from the prior tip; exactly one application CAS can win. |
| After application CAS, before log sequencing | New tip plus the full idempotency key and a pending operation | Resume that keyed operation and submit exactly one leaf; never roll back the committed tip. | Return deterministic `pending(current_tip, operation_key)` without changing state. |
| After leaf sequencing, before checkpoint publication | New tip, keyed operation, and exact leaf identity or index | Publish a checkpoint over the existing leaf; never submit the leaf again. | Return deterministic `pending(current_tip, operation_key)` without changing state. |
| After checkpoint publication, before witness cosigning | New tip, keyed operation, leaf identity, and checkpoint digest | Request cosigning for that same checkpoint and continue the same operation. | Return deterministic `pending(current_tip, operation_key)` without changing state. |
| After witness cosigning, before receipt persistence | New tip, keyed operation, leaf identity, checkpoint digest, and a replay-safe cosign request identity | Replay only that cosign request if needed, then CAS-store exactly one receipt for the key. | Return deterministic `pending(current_tip, operation_key)` without changing state. |
| After receipt persistence, before or during response | Complete stored original receipt keyed by `(namespace, expected_prior_tip, record_digest)` | Return that receipt byte-for-byte on every retry; append no leaf and create no second receipt. | Admit an ordinary successor against `current_tip`; the original key still returns the stored receipt. |

## Proof gaps that block provider selection

- No administrative model has been selected or authorized: managed authority,
  contracted independent operator, or separately administered custom service.
- No named account, tenant, recovery owner, billing owner, witness operator,
  issuer key, region, retention period, availability target, or cost ceiling is
  authorized.
- Google Cloud Storage, S3, and Azure Blob still require destructive retention,
  public-locator, account-deletion, billing, and recovery tests.
- Backblaze B2 lacks reviewed proof of exact conditional upload and whole-object
  SHA-256 handling at the authority boundary.
- R2 lacks proof of irreversible compliance retention against a privileged
  administrator.
- Tessera plus C2SP requires a new application-tip sequencer, receipt format,
  deployment, public monitor, independent witness operators, and service-level
  contract.
- Azure Confidential Ledger's preview UDF paths still lack qualified
  enforcement, concurrency, idempotency, receipt-binding, code-governance, and
  recovery behavior. Data-plane retrieval is authenticated, a control-plane
  administrator can hard-delete the ledger, and preview access and terms are not
  a production commitment.
- Rekor lacks application-tip CAS; Rekor v2's public witness network is not an
  available initial-launch property.

Until these gaps close, `hatchery_deployment_binding/v1` must remain unissued,
physical collection must not start, and every promotion unit remains at its
recorded fallback.
