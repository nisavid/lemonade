# Lemonade Fork Context

This context records an upstream-derived working vocabulary for agents operating in the `nisavid/lemonade` fork. Upstream Lemonade owns the canonical product language and architecture; use this file to find and apply that language, then verify against current upstream docs and source before making durable plans or changes.

## Source Authority

- Upstream source: <https://github.com/lemonade-sdk/lemonade>
- Upstream website and docs: <https://lemonade-server.ai/> and <https://lemonade-server.ai/docs/>
- Local fork policy: `AGENTS.md` and `docs/agents/`
- Local scout map: `docs/agents/research-map.md`

## Language

### Product and Runtime

**Lemonade**:
The local AI runtime and project that makes local text, vision, image, transcription, and speech capabilities available through standard APIs.
_Avoid_: Treating Lemonade as only a chat UI or only an LLM runner.

**Lemonade Server**:
The installable local service experience that exposes Lemonade capabilities to users, apps, and tools.
_Avoid_: Using this term when only the `lemond` executable is meant.

**Embeddable Lemonade**:
A portable `lemond` release artifact that app builders bundle privately into their own applications.
_Avoid_: Assuming it is the same deployment shape as the globally installed Lemonade Server service.

**lemond**:
The pure HTTP server process that loads configuration, registers routes, and delegates inference to backend subprocesses.
_Avoid_: `lemonade-server` when referring to the current server executable.

**lemonade CLI**:
The command-line client for controlling a running Lemonade service.
_Avoid_: Treating the CLI as the server process.

**Lemonade desktop app**:
The Tauri-based thin client that manages models and chats with a separately running `lemond`.
_Avoid_: Assuming the desktop app owns the server lifecycle.

**Lemonade web app**:
The browser build of the shared React renderer served by `lemond` for `/app`.
_Avoid_: Consolidating its package metadata with the desktop app.

**Tray app**:
The lightweight macOS/Linux tray client that connects to a running `lemond`.
_Avoid_: Confusing it with Windows `LemonadeServer.exe`.

**LemonadeServer.exe**:
The Windows GUI process that embeds `lemond` and shows the system tray icon.
_Avoid_: Treating it as the on-demand Tauri desktop app.

**Local server**:
Server software running on client hardware rather than cloud infrastructure.
_Avoid_: Assuming "local" always means "same process" or "same client machine."

### API and Client Integration

**OpenAI-compatible API**:
The primary standards-compatible API surface for chat, completions, embeddings, responses, audio, image, models, and realtime behavior.
_Avoid_: Treating Lemonade-specific lifecycle APIs as part of OpenAI compatibility.

**Ollama-compatible API**:
The `/api/*` compatibility surface for Ollama-oriented clients.
_Avoid_: Assuming unsupported Ollama create/copy/push flows are implemented.

**Anthropic-compatible API**:
The Messages-style compatibility endpoint for Claude-oriented clients.
_Avoid_: Assuming every Anthropic-specific field is fully implemented.

**Lemonade-specific API**:
The local-first lifecycle and management API for model pull/delete/load/unload, health, stats, system info, backend install, and logs.
_Avoid_: Exposing local management behavior through cloud API assumptions.

**Realtime API**:
The OpenAI-compatible WebSocket surface for real-time audio transcription.
_Avoid_: Assuming it shares the main HTTP server port.

**OmniRouter**:
Lemonade's approach to multimodal agentic workflows using standard model endpoints as OpenAI-compatible tools.
_Avoid_: Describing it as a proprietary agent runtime.

**Collection**:
A preconfigured multi-model bundle sized for a hardware tier and used by OmniRouter workflows.
_Avoid_: Calling user-facing collections "bundles" unless quoting internal code or history.

**Tool definition**:
A JSON schema entry that maps an OmniRouter tool name to a Lemonade endpoint and required model labels.
_Avoid_: Hand-authoring divergent tool schemas when the app's canonical JSON exists.

### Models, Recipes, and Backends

**Model registry**:
The built-in `server_models.json` catalog of Lemonade model entries.
_Avoid_: Treating it as a complete list of every possible Hugging Face model.

**Model name**:
The identifier used by APIs and CLI commands to refer to a registered, user, extra, or collection model.
_Avoid_: Confusing model names with Hugging Face checkpoint ids.

**Built-in model**:
A model shipped in the upstream registry.
_Avoid_: Editing built-in entries without checking recipe integrity and hardware filtering.

**User model**:
A custom model registered under the `user.` namespace.
_Avoid_: Storing the `user.` prefix inside `user_models.json` keys.

**Extra model**:
A GGUF model discovered from `extra_models_dir` under the `extra.` namespace.
_Avoid_: Deleting extra models through the API; those files are user-managed.

**Checkpoint**:
The Hugging Face repository, file, variant, or local path used to locate model artifacts.
_Avoid_: Assuming every checkpoint is a single GGUF file.

**Variant**:
The checkpoint suffix selecting a quantization, file, or sharded folder.
_Avoid_: Assuming omitted variants are always unambiguous.

**Recipe**:
The Lemonade model execution family, such as `llamacpp`, `flm`, `ryzenai-llm`, `vllm`, `whispercpp`, `sd-cpp`, `kokoro`, or `collection`.
_Avoid_: Using "recipe" and "backend" interchangeably.

**Backend**:
The concrete engine implementation or acceleration choice used by a recipe, such as Vulkan, ROCm, Metal, CPU, or NPU.
_Avoid_: Assuming all recipes expose the same backend choices.

**Backend version pin**:
The `backend_versions.json` entry that selects the upstream backend artifact Lemonade downloads.
_Avoid_: Updating backend pins without checking the corresponding recipe behavior and package/runtime effects.

**Recipe options**:
Per-model or global runtime settings that shape how a recipe launches and runs.
_Avoid_: Treating recipe options as model registry identity.

**Model pin**:
In the accepted fork residency model, a runtime property of a loaded model that vetoes automatic hard reclamation of its weights and backend process.
_Avoid_: Treating a runtime pin as durable configuration, startup loading, or a backend version pin.

**Saved pin preference**:
A durable per-model recipe option that asks `lemond` to apply a model pin whenever a completed load commits that model to residency.
_Avoid_: Treating the saved preference as proof that the model is currently loaded or pinned.

**Pinned-model startup loading**:
A server startup policy that, when enabled, attempts grouped admission of models with a saved pin preference.
_Avoid_: Folding startup loading into the definition of a model pin or enabling it implicitly for every user.

**Startup admission set**:
A connected set of saved pin preferences whose models share a residency constraint. Startup selects the set all-or-none: every member is jointly admissible, or no member receives precedence.
_Avoid_: Using registry, JSON, container, or execution order to choose which remembered pins become resident.

**Resolved admission intent**:
An immutable planning snapshot of a requested model's identity, recipe, backend, device, saved options, topology, count category, effective mode, configuration and signal generations, and predicted constraint effects.
_Avoid_: Building admission sets from nominal model names before their actual constraints are resolved.

**Provisional startup load**:
Backend resources materialized for a startup admission set but still owned exclusively by that plan. It is non-routable, is not published as loaded, and has no runtime pin until the complete set commits.
_Avoid_: Applying ordinary pin protection to transaction-owned cleanup or exposing a provisional member as independently available.

**Model residency**:
The current presence of model weights and backend state in the device-specific memory and process resources used for inference.
_Avoid_: Inferring residency from a saved pin preference or downloaded model files.

**In-use model**:
A resident model that is actively serving a request or an owned inference operation.
_Avoid_: Treating an in-use model as eligible for automatic reclamation.

**Admission reclamation**:
Reclaiming eligible resident model resources before a new load would exceed the configured capacity envelope.
_Avoid_: Waiting for a failed or destabilizing load before considering predictable capacity pressure.

**Pressure reclamation**:
Reclaiming eligible resident model resources in response to measured memory pressure, including pressure created by other applications.
_Avoid_: Treating pressure reclamation and admission reclamation as interchangeable triggers.

**Soft reclamation**:
Releasing reconstructible idle state, such as a KV cache, while preserving model weights, the backend process, and any model pin.
_Avoid_: Reporting soft reclamation as a model eviction.

**Hard reclamation**:
Unloading model weights and its backend process to recover capacity.
_Avoid_: Automatically applying hard reclamation to a pinned or in-use model.

**NPU exclusivity**:
The rule that an exclusive NPU recipe cannot coexist with conflicting NPU residents. In the accepted fork residency model, automatic admission may displace only conflicts that are both unpinned and idle; a pinned or in-use conflict vetoes the load.
_Avoid_: Giving an incoming pinned model precedence over a resident pin or assuming FLM coexistence rules apply to RyzenAI or whisper.cpp NPU models.

### Hardware and Engines

**CPU**:
The universal fallback device for supported x86_64 execution paths.
_Avoid_: Treating CPU support as a performance target for every model size.

**GPU**:
The graphics processor used through backends such as Vulkan, ROCm, Metal, vLLM ROCm, or sd-cpp ROCm.
_Avoid_: Assuming all GPU backends support the same vendors or operating systems.

**Residency memory domain**:
A memory-accounting boundary within which resident model weights and caches compete for capacity and share pressure signals. A domain may be discrete VRAM, GTT-backed shared memory, or another unified-memory pool.
_Avoid_: Using "VRAM" as a generic name for every domain or treating a host-memory safety floor as a second residency domain.

**GTT/shared GPU memory**:
System memory mapped into GPU address spaces for application allocations on unified-memory AMD systems. On Hatchery, model weights and KV caches are accounted in this pool.
_Avoid_: Adding GTT usage to host-memory usage as if they were independent physical allocations, or using nominal dedicated VRAM as Hatchery's model-residency capacity.

**Host-memory safety floor**:
An independent system-health guard that preserves enough memory for the operating system and other applications on shared-memory systems.
_Avoid_: Treating the safety floor as model-footprint accounting or as a second model-capacity pool.

**Residency constraint**:
A condition that a residency plan must satisfy, such as memory-domain headroom, the host-memory safety floor, a count-managed slot, or NPU/FLM compatibility. A constraint need not be a distinct memory-accounting pool.
_Avoid_: Adding aliased views of one physical allocation as if each constraint consumed separate bytes.

**Residency constraint claim**:
A reservation or occupancy recorded in the server-owned constraint ledger. Claims cover every affected constraint and move atomically through reserved, provisional, committed, quarantined, or released accounting states.
_Avoid_: Reserving only familiar memory or compatibility constraints while leaving count or adapter-declared constraints available to competing plans.

**Residency capability level**:
The evidence-backed status of one residency operation for a platform, backend, and affected domain set. **Validated** means end-to-end physical tests of that operation; **modeled** means primary-evidence architecture and signals without full physical validation; **fallback-only** means an explicit safe fallback without full capacity automation; **unsupported** means no safe residency behavior.
_Avoid_: Calling design coverage validation or implying that fallback-only behavior provides capacity-aware automation.

**Residency capability profile**:
The collection of per-operation capability levels for a platform and backend, including at least admission and measured-pressure reclamation. A combination-wide label may summarize the profile for display but is not a policy input.
_Avoid_: Assuming admission and external-pressure handling have the same signal availability or evidence.

**Footprint confidence class**:
The server-computed assurance that a predicted residency effect has a conservative bound suitable for a particular operation. A reviewed capability-catalog rule derives the class from recognized estimator or measurement provenance and uncertainty.
_Avoid_: Trusting an adapter-supplied confidence label or treating an unknown bound as zero.

**Residency capability catalog**:
A reviewed, versioned, content-addressed set of per-operation rules that maps an attested platform, backend, topology, configuration, recovery mechanism, and evidence set to one capability level, effective prerequisites, and fallback or refusal.
_Avoid_: Letting a live adapter promote its own capability or reinterpreting an existing claim more permissively after a catalog change.

**Effective residency mode**:
The behavior currently available for one residency operation and affected domain set after combining its capability level with the live health and freshness of required signals: capacity-aware automation, the declared conservative fallback, or refusal.
_Avoid_: Treating a validated capability level as proof that its sensors are currently available, fresh, or trustworthy.

**Residency plan**:
The server-owned decision and outcome for one admission, measured-pressure, NPU-conflict, or load-retry trigger, including its constraint snapshot and complete ordered action set.
_Avoid_: Treating a sequence of opportunistic per-model choices as one atomic plan.

**Residency plan reservation**:
An atomic server-owned claim on an operation's complete constraint-claim and ordered-action set before any irreversible action begins.
_Avoid_: Treating a per-model lease or a read-only preflight as plan-wide protection from concurrent admissions and lifecycle changes.

**Residency lifecycle mutation gate**:
The server-owned coordination boundary that fences topology, catalog, configuration, observation, recovery, journal, and backend-growth generations. Ordinary lifecycle work requires a fully ready token; narrowly scoped recovery remediation can only strengthen or reconcile blocked state and verify cleanup.
_Avoid_: Sampling independent readiness flags or adding an untyped recovery bypass around ordinary locking and claim rules.

**Suspended residency**:
Known-owned, fully bounded, non-routable residency whose current policy or evidence is invalid. Within one continuously fenced daemon epoch, it may return to active only after full re-attestation and claim reconciliation.
_Avoid_: Using suspension for uncertain ownership, effects, or release; those conditions require quarantine.

**Quarantined residency**:
An ambiguous backend or resource state that is not routable and conservatively retains every plausible constraint claim until cleanup verifies release.
_Avoid_: Crediting memory, count, or compatibility capacity merely because an unload or rollback returned an uncertain result, or reactivating quarantine instead of releasing it.

**Backend recovery authority**:
A platform/backend guarantee established before subprocess spawn that lets `lemond` recover ownership safely after interruption. A launch uses exactly one reviewed mode: hereditary containment that covers every future resource-owning descendant, or durable membership that externally prevents descendant creation and resource acquisition until each owner is journaled and atomically bound. Both modes retain ownership evidence until complete release is verified.
_Avoid_: Using saved pin preferences or a reusable PID as ownership evidence, or treating operational recovery metadata as desired configuration or proof of commit.

**Daemon recovery barrier**:
An exclusive startup phase that quarantines every prior-epoch ownership record and constraint claim before any model-lifecycle request or automatic residency plan can run. Recovery does not adopt surviving backends; it proves ownership before cleanup and releases claims only after complete resource-tree release.
_Avoid_: Starting the pin loader, pressure engine, or ordinary load handling while survivor ownership or prior claims remain unreconciled, or restoring a prior process to active merely because its journal record was valid.

**NPU**:
The neural processing unit targeted by RyzenAI and FastFlowLM paths.
_Avoid_: Assuming NPU behavior is cross-platform or unconstrained.

**Ryzen AI**:
AMD's AI PC platform and NPU software ecosystem used by Lemonade NPU and hybrid paths.
_Avoid_: Assuming every Ryzen CPU has the same NPU capability.

**XDNA2 NPU**:
The NPU family targeted by current Lemonade FastFlowLM and RyzenAI NPU support.
_Avoid_: Generalizing to older Ryzen AI NPUs without source evidence.

**llama.cpp**:
The primary LLM inference recipe family for GGUF models across CPU, Vulkan, ROCm, Metal, and system backends.
_Avoid_: Assuming llama.cpp itself downloads models at request time.

**FastFlowLM**:
The `flm` recipe family for NPU LLM, embedding, reranking, and audio paths.
_Avoid_: Applying non-FLM model download assumptions to FLM models.

**RyzenAI**:
The `ryzenai-llm` recipe family for Windows NPU and hybrid model execution.
_Avoid_: Assuming it is the Linux NPU path.

**vLLM**:
The experimental ROCm-on-Linux recipe for high-concurrency LLM inference on supported AMD GPUs.
_Avoid_: Treating it as broadly validated outside the documented GPU targets.

**whisper.cpp**:
The speech-to-text recipe family for audio transcription.
_Avoid_: Treating text-to-speech behavior as whisper.cpp behavior.

**stable-diffusion.cpp**:
The `sd-cpp` recipe family for image generation, editing, variations, and upscaling.
_Avoid_: Calling the recipe `stable-diffusion.cpp` in JSON where `sd-cpp` is expected.

**Kokoro**:
The text-to-speech recipe family.
_Avoid_: Confusing Kokoro TTS with whisper.cpp speech-to-text.

**GGUF**:
The common llama.cpp model artifact format used for many LLM, embedding, reranking, and vision models.
_Avoid_: Assuming GGUF implies text-only.

**ONNX**:
The model artifact family used by RyzenAI/OGA-style optimized model paths.
_Avoid_: Treating ONNX models as llama.cpp-compatible.

**Hugging Face cache**:
The primary model storage layout Lemonade uses for downloaded model artifacts.
_Avoid_: Replacing cache path logic with host-specific hardcoded paths.

### Design Tenets

**Foundation, not the house**:
Lemonade should let users start in the app, connect other apps, and eventually embed Lemonade invisibly.
_Avoid_: Making the GUI the center of every workflow.

**Happy path**:
The default user journey should stay simple even when advanced configuration exists.
_Avoid_: Exposing advanced backend complexity before users need it.

**Standards are intuitive**:
Prefer existing standards such as OpenAI APIs, Ollama conventions, and familiar GUI patterns.
_Avoid_: Inventing a Lemonade-specific concept when a standard one fits.

**Automate the documentation away**:
Complex documentation is treated as a signal that a feature may need more automation.
_Avoid_: Solving user confusion only by adding more prose.

**User error is a bug**:
Common user mistakes should drive UX, validation, and documentation improvements.
_Avoid_: Blaming users for plausible input.

**Backends are fungible**:
Users and builders should be able to switch viable backend choices with minimal friction.
_Avoid_: Promoting one backend as intrinsically superior across all contexts.

**Design for agility**:
The project values day-0 model and engine access, with features designed to accelerate future development.
_Avoid_: Hardcoding assumptions that slow backend/model refreshes.

### Fork Operation

**Upstream Lemonade**:
The `lemonade-sdk/lemonade` project, website, maintainers, docs, and release stream that define the baseline product.
_Avoid_: Treating fork-local agent notes as upstream authority.

**Fork origin**:
The `nisavid/lemonade` repository where this work is pushed by default.
_Avoid_: Assuming a change is intended for upstream submission.

**Fork-local change**:
A change intended to help this fork's operation, exploration, packaging, or agent guidance without upstream submission.
_Avoid_: Reframing fork-local work as an upstream contribution.

**Upstream contribution exception**:
An explicit user-directed change in which upstream submission, upstream issues, or upstream PRs are in scope.
_Avoid_: Opening upstream-facing artifacts without the user's explicit direction.

## Relationships

- **Lemonade Server** runs **lemond**, and clients interact with **lemond** over HTTP or WebSocket APIs.
- The **lemonade CLI**, **Lemonade desktop app**, **Lemonade web app**, **tray app**, and third-party apps are clients of **lemond**.
- **lemond** delegates model lifecycle and inference work to backend subprocesses.
- A **model name** resolves to model metadata, a **recipe**, **checkpoints**, labels, loaded-model category, and recipe options.
- A **recipe** selects the execution family; a **backend** selects the concrete engine or device path inside that family.
- In the accepted fork residency model, a **Model pin** protects current residency; a **saved pin preference** controls pinning on a future load; **pinned-model startup loading** decides whether those preferences cause startup admission.
- **Pinned-model startup loading** partitions remembered pins into **startup admission sets** connected by shared constraints. Each set is selected all-or-none; independent feasible sets may still load.
- A **startup admission set** is built from immutable **resolved admission intents**. Its members remain **provisional startup loads** until one atomic commit publishes the complete set and applies runtime pins.
- In that target, **admission reclamation** and **pressure reclamation** use different triggers but share eligibility rules: an **in-use model** or **Model pin** vetoes automatic hard reclamation.
- **Soft reclamation** may preserve a pin because model weights and the backend remain resident; **hard reclamation** removes residency.
- A **residency memory domain** is the capacity and pressure boundary for model residency. Hatchery's **GTT/shared GPU memory** is one such domain; its **host-memory safety floor** constrains the same physical system memory without becoming model-footprint accounting or another capacity pool.
- A **residency plan** must satisfy every **residency constraint**. Reclaiming one shared allocation may improve both GTT headroom and the host floor without creating two independently reclaimable allocations.
- A **residency capability profile** records evidence per operation and domain set; the **effective residency mode** for each cell also depends on the current health of that operation's required signals.
- A **residency plan reservation** atomically acquires every **residency constraint claim** and action lease. Verified outcomes convert those claims to committed occupancy or release them; ambiguous outcomes transfer them to **quarantined residency**.
- Every backend spawn requires a **backend recovery authority**. Desired residency remains in recipe options; transient ownership metadata exists only to identify and safely clean up runtime resources after interruption.
- A new `lemond` owner crosses the **daemon recovery barrier** before model lifecycle becomes ready. Surviving resources enter **quarantined residency** until ownership and release are verified.
- Loaded-model categories control per-type LRU slots; **NPU exclusivity** controls cross-type NPU conflicts without overriding pins or in-use protection.
- **OmniRouter** uses **Collections** and **tool definitions** to expose multi-modal endpoints through the standard tool-calling loop.
- **Fork-local changes** target **fork origin** unless the user declares an **upstream contribution exception**.

## Example dialogue

> **Agent:** "Should this plan add a new Lemonade model registry entry or just document a custom Hugging Face pull flow?"
> **Fork owner:** "Check upstream intent first. If upstream already treats `lemonade pull owner/repo` as the happy path, prefer docs or UX around that path. Do not make a registry change unless the source evidence says a built-in model entry is appropriate."
>
> **Agent:** "Should I file this against upstream?"
> **Fork owner:** "No. Treat it as fork-local unless I explicitly say upstream is in scope."

## Flagged ambiguities

- "Server" may mean **Lemonade Server**, **lemond**, `LemonadeServer.exe`, or a backend subprocess. Name the specific process or product surface.
- "Backend" may mean a recipe family, an acceleration option, or an upstream executable. Use **recipe** for Lemonade model execution families and **backend** for the concrete engine/device choice.
- "App" may mean the Tauri desktop app, the browser web app, a tray client, or a third-party client. Name the client surface.
- "Local" means local-first and user-controlled, not necessarily same-process or same-machine.
- "Collection" is the current user-facing term for OmniRouter multi-model bundles; use older names only when reading internal identifiers or historical commits.
- "Pin" may mean a current **Model pin** or a **saved pin preference**. Name the durable preference explicitly, and name **pinned-model startup loading** when startup behavior is meant.
