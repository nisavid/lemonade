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
A server-owned model lifecycle preference that asks `lemond` to keep a model resident across startup and automatic eviction pressure.
_Avoid_: Treating it as a desktop-app-local preference or as a backend version pin.

**NPU exclusivity**:
The rule that exclusive NPU recipes evict other NPU models before loading.
_Avoid_: Assuming FLM coexistence rules apply to RyzenAI or whisper.cpp NPU models.

### Hardware and Engines

**CPU**:
The universal fallback device for supported x86_64 execution paths.
_Avoid_: Treating CPU support as a performance target for every model size.

**GPU**:
The graphics processor used through backends such as Vulkan, ROCm, Metal, vLLM ROCm, or sd-cpp ROCm.
_Avoid_: Assuming all GPU backends support the same vendors or operating systems.

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
- A **Model pin** is stored in `lemond` configuration and edited by clients through server APIs.
- Loaded-model categories control per-type LRU slots; **NPU exclusivity** controls cross-type NPU eviction.
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
