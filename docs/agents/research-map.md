# Lemonade Research Map

Use this map to scout Lemonade without bloating every future prompt. It records where to look, what each source is good for, and which assumptions need checking.

Initial scout date: 2026-05-14. The local fork base matched upstream `lemonade-sdk/lemonade` `main` at commit `bdd642230172e917f01e92849ca14e19eb2e897a` before fork-local agent guidance changes.

## Live Primary Sources

- Product site: <https://lemonade-server.ai/>
- Documentation root: <https://lemonade-server.ai/docs/>
- Upstream source: <https://github.com/lemonade-sdk/lemonade>
- Latest releases: <https://github.com/lemonade-sdk/lemonade/releases>

Prefer live upstream sources for time-sensitive details such as latest releases, supported platforms, backend pins, model lists, or current website language.

## Local Source Routes

### Product shape and philosophy

- `README.md`: product overview, supported configurations, model library, roadmap, integration examples, maintainer contacts, privacy policy.
- `docs/README.md`: documentation entrypoint for users, contributors, builders, and embedders.
- `docs/dev/philosophy.md`: mission and design tenets.
- `docs/dev/contribute.md`: upstream contribution process, reviewer expectations, maintainer areas.

### User, CLI, and configuration behavior

- `docs/guide/README.md`: user-guide entrypoint.
- `docs/guide/concepts.md`: local server and OpenAI-standard framing.
- `docs/guide/cli.md`: `lemonade` CLI commands and options.
- `docs/guide/configuration/README.md`: `config.json`, API keys, remote access, backend binary selection.
- `docs/guide/configuration/custom-models.md`: `user_models.json`, `recipe_options.json`, checkpoints, labels.
- `docs/guide/configuration/multi-model.md`: model type slots, LRU, and device constraints.
- `docs/guide/configuration/llamacpp.md`: llama.cpp backend choices and ROCm channels.
- `docs/guide/configuration/vllm.md`: experimental vLLM ROCm path and gotchas.

### API surfaces

- `docs/api/README.md`: endpoint families and design philosophy.
- `docs/api/openai.md`: OpenAI-compatible endpoints and request/response shapes.
- `docs/api/ollama.md`: Ollama compatibility surface and unsupported routes.
- `docs/api/anthropic.md`: Anthropic Messages compatibility scope.
- `docs/api/lemonade.md`: Lemonade-specific lifecycle, model, backend, health, stats, and system endpoints.
- `docs/api/llamacpp.md`: llama.cpp-specific compatibility behavior.

### Embeddable and app surfaces

- `docs/embeddable/README.md`: embeddable artifact, customization, and deployment-ready layout.
- `docs/embeddable/runtime.md`: subprocess runtime expectations for embedded `lemond`.
- `docs/embeddable/backends.md`: backend deployment timing and installation strategy.
- `docs/embeddable/models.md`: bundled, shared, and private model behavior.
- `docs/dev/app.md`: Tauri desktop app topology and thin-client constraints.
- `docs/dev/web-ui.md`: browser web app build split and Debian native packaging constraint.

### C++ server architecture

- `src/cpp/server/server.cpp`: HTTP route registration, auth, web app serving, handlers.
- `src/cpp/server/router.cpp` and `src/cpp/include/lemon/router.h`: model routing, LRU eviction, NPU exclusivity, busy protection.
- `src/cpp/server/model_manager.cpp` and `src/cpp/include/lemon/model_manager.h`: registry loading, user models, extra models, path resolution, downloads.
- `src/cpp/include/lemon/wrapped_server.h`: backend subprocess abstraction.
- `src/cpp/include/lemon/server_capabilities.h`: completion, embedding, reranking, audio, image, TTS, and slots capability interfaces.
- `src/cpp/include/lemon/model_types.h`: model type and device type classification.
- `src/cpp/resources/server_models.json`: built-in model registry.
- `src/cpp/resources/backend_versions.json`: backend artifact version pins.

### Frontend architecture

- `src/app/src/renderer/`: shared React renderer for desktop and web.
- `src/app/src/renderer/tauriShim.ts`: desktop `window.api` bridge.
- `src/app/src/renderer/utils/toolDefinitions.json`: canonical OmniRouter tool definitions.
- `src/app/src-tauri/`: Tauri Rust host, settings, beacon discovery, window and event plumbing.
- `src/web-app/`: browser build wrapper around the shared renderer.

For architecture relationships that should not live in the domain glossary, see `docs/agents/architecture-map.md`.

## Common False Assumptions

- Do not assume `lemonade-server` is the current server executable; `lemond` is the server and `lemonade-server` is a deprecated compatibility shim.
- Do not assume the desktop app starts or owns `lemond`; it is a thin client for a separately running server.
- Do not consolidate `src/app/package.json` and `src/web-app/package.json`; the split supports Debian native packaging.
- Do not register new HTTP endpoints under only one version prefix; OpenAI-compatible routes use `/api/v0/`, `/api/v1/`, `/v0/`, and `/v1/`.
- Do not assume `Lemonade Server`, `lemond`, `LemonadeServer.exe`, and backend subprocesses are interchangeable.
- Do not treat `recipe` and `backend` as synonyms.
- Do not assume all NPU recipes can coexist; read the router's recipe-aware NPU rules.
- Do not assume model names and Hugging Face checkpoint ids are the same identifier.
- Do not assume `Collection` entries are real single models in OpenAI-compatible model listings.
- Do not push, file, or describe work as upstream-bound unless the user explicitly asks for an upstream contribution.

## When Research Produces Durable Knowledge

Update the smallest durable surface:

- Stable vocabulary or relationships: `CONTEXT.md`.
- A new source route or likely false assumption: this file.
- A compact implementation relationship or architecture guardrail: `docs/agents/architecture-map.md`.
- A fork policy rule: `docs/agents/fork-stewardship.md`, with a short pointer in `AGENTS.md` if it must always load.
- A hard-to-reverse, surprising trade-off decision: `docs/adr/`.
