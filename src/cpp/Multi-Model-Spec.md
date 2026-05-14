# Multi-Model Specification

Lemonade Server supports loading multiple models simultaneously. Each model runs in its own WrappedServer instance, and a set of rules governs when WrappedServers are evicted or kept in memory.

## Eviction Logic

### Max Loaded Models

The `--max-loaded-models N` argument controls how many WrappedServers can be in memory per type slot. The same limit applies to every model type. Use `-1` for unlimited (no LRU eviction).

Models are categorized into five types:
- `llm`: Chat and completion models (default type)
- `embedding`: Models with the `embedding` label
- `reranking`: Models with the `reranking` label
- `audio`: Models with the `audio` label (e.g., Whisper)
- `image`: Models with the `image` label (e.g., Stable Diffusion)

The default value is `1`, meaning one model of each type can be loaded simultaneously.

Examples:
- `--max-loaded-models 3` → up to 3 LLMs, 3 embedding, 3 reranking, 3 audio, 3 image
- `--max-loaded-models 5` → up to 5 of each type
- `--max-loaded-models -1` → unlimited (no eviction based on slot count)

ModelInfo and WrappedServer include an explicit enum field for tracking the model type. This field is used throughout the codebase (e.g., in `llamacpp_server.cpp`) to adjust settings based on model type.

### GPU Memory Occupancy

The `max_gpu_memory_occupancy_gb` config key controls the maximum GPU memory Lemonade may occupy. Use `-1` for auto, which uses detected GPU capacity. For each load, the active capacity is the minimum of:
- the configured capacity, or detected total capacity when configured as auto
- free GPU memory plus the GPU memory currently occupied by loaded Lemonade models

Before loading a GPU model, Router predicts the candidate model's occupancy. It uses GGUF metadata for llama.cpp models when available, including layer count, KV head count, key/value length, and configured context size for a conservative KV-cache estimate. If metadata is missing, it falls back to model size plus overhead.

If projected occupancy exceeds the active capacity, Router dry-runs a largest-to-smallest eviction plan using loaded models' measured occupancy. If the candidate fits after the planned evictions, Router evicts those models and proceeds. If the candidate cannot fit even after evicting all eligible loaded GPU models, the load fails without evicting anything.

### Least Recently Used Cache

When loading a new WrappedServer of a given TYPE:
1. If a slot is available for that TYPE, the new WrappedServer loads without evicting existing ones.
2. If no slot is available, the least-recently-used WrappedServer of that TYPE is evicted first.

Each WrappedServer maintains a timestamp of its most recent access. The following operations update this timestamp:
- WrappedServer load start and completion
- Inference request start and completion (chat/completions, completions, responses, embedding, reranking)

### Error Handling

If a WrappedServer load fails (with exceptions noted below), all WrappedServers of every type are evicted, and the load is re-attempted.

This "nuclear" policy simplifies implementation while remaining effective in practice.

**Exception:** Models are not evicted if the load failed because a file was not found on disk.

### NPU Exclusivity

Only one model can occupy the NPU at a time. The following recipes use the NPU:
- `ryzenai-llm`
- `flm`

When loading a WrappedServer with an NPU recipe, any existing NPU-using WrappedServer is evicted regardless of type or `--max-loaded-models` settings.

> Note: CPU memory is not tracked. GPU loads are constrained by `max_gpu_memory_occupancy_gb` when GPU capacity can be detected.

**Recipe to Device Mapping:**
| Recipe | Device(s) |
|--------|-----------|
| `llamacpp` | gpu |
| `ryzenai-llm` | npu |
| `flm` | npu |

ModelInfo and WrappedServer include a bitmask enum field for tracking target devices, enabling checks like `if (model.device & Device::NPU)`.

## Concurrency

1. **Busy WrappedServers are protected:** A WrappedServer actively fulfilling an inference request cannot be evicted until it finishes. Pending loads queue indefinitely.

2. **Serialized loading:** Only one WrappedServer loads at a time. Eviction decisions are made when a queued load begins (not when it enters the queue).

3. **Auto-load protection:** When a WrappedServer is auto-loaded for an inference request, the request executes before that WrappedServer becomes eligible for eviction.
   - Auto-load: If an inference request arrives for an unloaded model, Lemonade loads a WrappedServer for that model, then forwards the request.
