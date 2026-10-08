# llama.cpp Backend Options

Lemonade uses [llama.cpp](https://github.com/ggerganov/llama.cpp) as its primary LLM inference backend, supporting multiple hardware acceleration options. This document explains the available backends and how to choose between them.

## Available Backends

### CPU
> **macOS:** Lemonade uses the Metal-enabled llama.cpp build as the unified
> runtime. The same build can run CPU-only when GPU offload is disabled, so a
> separate `llamacpp:cpu` backend is not required on macOS.

- **Platform**: Windows, Linux
- **Hardware**: x86_64 processors (Windows and Linux); ARM64/aarch64 processors (Linux)
- **Use Case**: CPU-only execution and universal fallback on Windows and Linux
- **Performance**: Slowest option, suitable for small models or testing
- **Installation**: Automatically available via upstream llama.cpp releases

### Vulkan
- **Platform**: Windows, Linux
- **Hardware**: AMD GPUs (iGPU and dGPU), NVIDIA GPUs, Intel GPUs, Qualcomm Adreno and other Vulkan-capable GPUs on ARM64 Linux
- **Use Case**: Cross-vendor GPU acceleration
- **Performance**: Good performance across all GPU vendors
- **Installation**: Automatically available via upstream llama.cpp releases
- **Notes**: Recommended for most GPU users; on ARM64 Linux (e.g., Qualcomm X Elite), Vulkan is the default backend

### ROCm
- **Platform**: Windows, Linux
- **Hardware**: AMD Radeon RX 6000/7000 series (RDNA2/RDNA3/RDNA4), AMD Ryzen AI iGPUs (Strix Point/Halo)
- **Use Case**: AMD GPU-optimized inference
- **Performance**: Optimized for AMD hardware, may outperform Vulkan on supported GPUs
- **Channel Options**:
  - **Stable** (default): Custom builds with latest optimizations from lemonade-sdk
  - **Nightly**: Bleeding-edge builds from lemonade-sdk/llamacpp-rocm (experimental)
- **Installation**: Varies by channel (see below)

### CUDA
- **Platform**: Windows, Linux
- **Hardware**: NVIDIA GPUs with Compute Capability 7.5+ (Turing, Ampere, Ada, Hopper, Blackwell)
- **Use Case**: NVIDIA GPU-optimized inference
- **Performance**: Optimized for NVIDIA hardware, typically outperforms Vulkan on supported GPUs
- **Source**: Per-architecture builds from [lemonade-sdk/llama.cpp](https://github.com/lemonade-sdk/llama.cpp)
- **Binaries**: Compute-capability-specific builds (sm_75, sm_80, sm_86, sm_89, sm_90, sm_100, sm_120)
- **Runtime**: Bundled CUDA runtime libraries (no system-wide CUDA toolkit installation required)
- **Notes**: On Windows, .7z extraction requires the bsdtar bundled with Windows 11 22H2+. On Linux, the build is shipped as .tar.xz and extracts with the system `tar`.

### Metal
- **Platform**: macOS only
- **Hardware**: Apple Silicon (M1/M2/M3/M4) and Intel Macs with Metal support
- **Use Case**: Accelerated llama.cpp inference on macOS
- **Performance**: Optimized for Apple Silicon
- **Installation**: Automatically available via upstream llama.cpp releases
- **Notes**: The Metal-enabled build can also run CPU-only when GPU offload is disabled

### System
- **Platform**: Linux only
- **Hardware**: Depends on system-installed llama-server binary
- **Use Case**: Advanced users with custom llama.cpp builds
- **Performance**: Depends on build configuration
- **Installation**: Requires manual installation of `llama-server` in system PATH
- **Enable**: Set `"llamacpp": { "backend": "system" }` in `config.json` to enable the system backend, or use `lemonade config set llamacpp.backend=system`
- **HIP plugin on non-standard paths**: When an AMD GPU is present, the system backend needs the GGML HIP plugin (`libggml-hip.so`). Lemonade looks for it in the standard system library paths. If your distribution or package manager installs it elsewhere (e.g. NixOS, a custom prefix, or a manual build), set `LEMONADE_GGML_HIP_PATH` to the full path of the plugin so the backend is reported as available:

  ```bash
  export LEMONADE_GGML_HIP_PATH=/opt/rocm/lib/libggml-hip.so
  ```

  The filename must look like `libggml-hip*.so*` (versioned sonames such as `libggml-hip.so.0` are accepted). This Linux-only variable is used solely to detect plugin availability; it is not forwarded to the GGML loader, so it does not change where llama.cpp actually loads the plugin from.

## ROCm Channel Configuration

The ROCm backend supports three channels to balance stability, performance, and access to latest features:

### Stable Channel (Default)
```json
{
  "rocm_channel": "stable"
}
```
- **Source**: Custom builds from [lemonade-sdk/llama.cpp](https://github.com/lemonade-sdk/llama.cpp)
- **Binaries**: Common builds for supported architectures
- **Updates**: Frequent updates with latest optimizations and fixes
- **Platform**: Windows and Linux
- **Runtime**: Requires runtime for both Windows and Linux to be installed separately.
- **Best For**: Users who want the latest performance optimizations

### Nightly Channel
```json
{
  "rocm_channel": "nightly"
}
```
- **Source**: Nightly builds from [lemonade-sdk/llamacpp-rocm](https://github.com/lemonade-sdk/llamacpp-rocm)
- **Binaries**: Architecture-specific builds (gfx1150, gfx1151, gfx103X, gfx110X, gfx120X)
- **Updates**: Nightly builds with experimental features and latest upstream changes
- **Platform**: Windows and Linux
- **Runtime**: Bundled runtime on Linux, TheRock ROCm dependencies
- **Best For**: Developers and testers who want bleeding-edge features and are comfortable with potential instability

### Changing Channels

To switch between channels, update your `config.json`:

```json
{
  "rocm_channel": "stable"
}
```

Or use the Lemonade CLI:
```bash
# Switch to stable channel (default)
lemonade config set rocm_channel=stable

# Switch to nightly channel (experimental)
lemonade config set rocm_channel=nightly
```

After changing channels, you'll need to reinstall the ROCm backend:
```bash
lemonade backends install llamacpp:rocm
```

### Reusing a System-Installed ROCm (Windows and Linux)

On the stable channel Lemonade normally downloads its own ROCm runtime. By default it installs the ROCm runtime from AMD's pip wheels into a lemonade-managed virtual environment (under `therock-wheels/<arch>-<version>/` in the cache), and falls back to the TheRock tarball (under `therock/<arch>-<version>/`) when Python/venv/pip is unavailable or the wheel install can't serve your GPU. See [`rocm_install_method`](#choosing-the-rocm-install-method) to force one path.

If you already have ROCm installed system-wide, Lemonade reuses it instead of downloading a second copy when it can find a matching version. It locates the install root in this order, using the first directory that contains the HIP runtime (`bin\amdhip64.dll` or `bin\amdhip64_<version>.dll` on Windows, `lib{,64}/libamdhip64.so` on Linux):

1. The `ROCM_PATH` environment variable
2. `rocm-sdk path --root`, when `rocm-sdk` is on your `PATH` (e.g. a ROCm installed from the TheRock pip wheels)
3. The platform default: `HIP_PATH` (set by the AMD HIP SDK installer) on Windows, `/opt/rocm` on Linux

`ROCM_PATH` and `rocm-sdk` work on both platforms. When the runtime is found via one of them, a `major.minor` version match is accepted (and a runtime with no version file is accepted as-is), so a patch-level difference won't trigger a second download. To force Lemonade to use a specific ROCm, set `ROCM_PATH` before starting the server:

```bash
# Linux
export ROCM_PATH=/path/to/rocm
```

```powershell
# Windows (PowerShell)
$env:ROCM_PATH = "C:\path\to\rocm"
```

### Choosing the ROCm Install Method

`rocm_install_method` controls how Lemonade installs its bundled ROCm runtime:

| Value | Behavior |
|---|---|
| `auto` *(default)* | Install from pip wheels; fall back to the TheRock tarball when Python/pip is unavailable or the wheels can't serve the GPU. |
| `wheel` | Use pip wheels only; fail rather than fall back to the tarball. |
| `tarball` | Use the TheRock tarball only; never invokes Python or pip. Use this for Python-averse, air-gapped, or pip-restricted environments. |

```bash
lemonade config set rocm_install_method=tarball
```

It can also be set with the `LEMONADE_ROCM_INSTALL_METHOD` environment variable.

### Pinning to a Specific Version Tag

You can pin `llamacpp.rocm_bin` to a specific release tag instead of using `"builtin"` or `"latest"`. **Each channel downloads from a different GitHub repository, so you must set the correct channel before setting a specific tag.**

| Channel | Repository | Tag format |
|---|---|---|
| `stable` *(default)* | [lemonade-sdk/llama.cpp](https://github.com/lemonade-sdk/llama.cpp) | Lemonade-specific build tags |
| `nightly` | [lemonade-sdk/llamacpp-rocm](https://github.com/lemonade-sdk/llamacpp-rocm) | Nightly tags, e.g. `b1260` |

> **Always set `rocm_channel` to the correct channel before setting `rocm_bin` to a specific tag.** If the tag does not exist in the current channel's repository, the download will fail with HTTP 404.

Example — pin to a specific nightly build:
```bash
# 1. Switch to the nightly channel first
lemonade config set rocm_channel=nightly

# 2. Then pin to the desired nightly tag
lemonade config set llamacpp.rocm_bin=b1260
```

## Per-Model Executable Bindings

A binding runs one model on a specific `llama-server` executable instead of the shared llama.cpp install. Every other model keeps the normal backend selection. Bindings suit a model that needs its own llama.cpp build, such as one with a feature the shared build lacks.

A binding is keyed by the model's cache key: the bare name for a built-in model (for example `Qwen3-4B-Instruct-2507-GGUF`, never `builtin.Qwen3-4B-Instruct-2507-GGUF`), or the `user.` or `extra.` ID otherwise. Each entry has exactly two fields:

| Field | Meaning |
|-------|---------|
| `executable` | Absolute path of the `llama-server` file to run, written without `.` or `..` segments. It names the file itself, never a directory, and is never looked up on `PATH`. |
| `backend` | `cpu`, `vulkan`, `rocm`, `cuda`, or `metal`, limited to the backends llama.cpp supports on this OS. It sets the device class and which `llamacpp.<backend>_args` apply. It never installs or selects a build, resolves a ROCm channel, or adds library paths: the executable brings its own runtime. |

### Where Bindings Come From

- **Package fragments.** Lemonade reads every `*.json` file (except dot files) in `/usr/share/lemonade/llamacpp-bindings.d/` on Linux and macOS, in name order. Set `LEMONADE_LLAMACPP_BINDINGS_DIR` to read another directory instead; on Windows that is the only fragment directory. Each fragment binds exactly one model and is named after its cache key, so `example-model.json` holds:

  ```json
  {
    "llamacpp": {
      "model_executables": {
        "example-model": {
          "executable": "/opt/example-llama/bin/llama-server",
          "backend": "vulkan"
        }
      }
    }
  }
  ```

- **`config.json`.** The same `llamacpp.model_executables` map. An entry replaces a fragment's entry for that model whole, with no field merge, and `null` unbinds the model so it uses the shared install again:

  ```json
  {
    "llamacpp": {
      "model_executables": {
        "example-model": null,
        "user.my-local-model": {
          "executable": "/opt/my-build/bin/llama-server",
          "backend": "rocm"
        }
      }
    }
  }
  ```

Nothing outranks `config.json`. Bindings are read once at startup, so restart `lemond` after changing them; `lemonade config set` and `/internal/set` reject the key. The key cannot appear in a defaults file, request options, saved model options, or a model registration.

### How a Bound Model Loads

- `lemond` starts the bound executable, whether or not the shared backend is installed, including with `no_fetch_executables` on.
- The bound backend replaces `llamacpp.backend`, architecture defaults, and any saved or registered `llamacpp_backend` for that model; a conflicting saved value is ignored with a warning in the log. `llamacpp.<backend>_args` for the bound backend apply as for any other model on that backend, and so does `llamacpp.device`, which must match the bound backend.
- A request that names a different `llamacpp_backend` for a bound model fails with an error, on `/load` and on every path that saves model options: `POST /models/{id}/options`, `POST /models/register`, pull, import, and collection component registration. Omitting the option, or sending `null`, `""`, `auto`, or `-1`, proceeds. To clear a stale saved backend, send `null` to `POST /models/{id}/options`.
- Each load checks that the executable is a regular file that `lemond` can run. When that check fails, the load fails with that error before any other model is evicted. When the bound executable fails to start, the load fails with its original error, and `lemond` does not evict every other model and retry; models that were already unloaded to make room for it stay unloaded.
- Changing a `llamacpp.*_bin` value or uninstalling a llama.cpp backend never unloads a bound model.

### Errors and Visibility

- A malformed fragment fails only the loads of the model its file name names, until it is fixed and `lemond` restarts. A valid `config.json` entry or `null` for that model takes precedence. A malformed `config.json` entry fails only its own model's loads and never falls back to a fragment or the shared install.
- Some binding errors name no model: `llamacpp.model_executables` in `config.json` is not an object, the fragment directory cannot be read, or a fragment file name or `config.json` key names no model, such as `builtin..json` or `user.`. Each one fails every llama.cpp load until the problem is fixed and `lemond` restarts, and every llama.cpp model stays listed so its loads report the error. Other backends keep working. An unparseable `config.json` is still renamed to `config.json.corrupted` and read as empty, as for any other setting, so only fragment bindings apply.
- At startup, `lemond` logs each binding with its source and warns about rejected entries, overridden fragments, executables that do not exist yet, and keys that are not a model's cache key, such as a registered model's name without its `user.` prefix. Such a key binds nothing.
- `lemonade config` (`GET /internal/config`) shows the effective table: each bound model's `executable`, `backend`, and `source` (the fragment path or `config.json`), `null` for a model unbound in `config.json`, and `error` with `source` for a rejected entry.
- `/health` reports each loaded model's `launch_command`, which starts with the bound executable.

## Choosing the Right Backend

### Decision Tree

1. **Do you have an NVIDIA GPU (Turing or newer)?**
   - Try **CUDA** first for best performance
   - Fall back to **Vulkan** if you encounter issues

2. **Do you have an AMD GPU?**
   - **For Radeon RX 6000/7000 or Ryzen AI iGPU**:
     - Try **ROCm** first for best performance
     - Fall back to **Vulkan** if you encounter issues
   - **For older AMD GPUs (RX 5000 and earlier)**:
     - Use **Vulkan** (ROCm not supported)

3. **Do you have an Intel GPU or older NVIDIA GPU?**
   - Use **Vulkan**

4. **Are you using macOS?**
   - Use **Metal**
   - The Metal-enabled build can also run CPU-only when GPU offload is disabled; no separate CPU backend selection is required

5. **No GPU or unsupported GPU?**
   - Use **CPU**

### ROCm Channel Selection

- **Use Stable** if you:
  - Prefer stability over latest features
  - Want upstream llama.cpp compatibility
  - Are deploying in production

- **Use Nightly** if you:
  - Want bleeding-edge experimental features
  - Are testing unreleased llama.cpp functionality
  - Are comfortable with potential bugs and instability
  - Are a developer contributing to lemonade or llama.cpp

## Platform Specifics

### Linux
- All backends supported (CPU, Vulkan, ROCm, CUDA, System)
- CPU and Vulkan backends support both x86_64 and ARM64 (aarch64) systems; on ARM64, Vulkan is the default
- ROCm requires compatible AMD GPU (see above)
- CUDA requires compatible NVIDIA GPU (see above)
- System backend requires manual llama-server installation

### Windows
- Supported: CPU, Vulkan, ROCm, CUDA
- ROCm requires compatible AMD GPU
- CUDA requires compatible NVIDIA GPU and Windows 11 22H2+ (for bundled bsdtar that extracts .7z assets)
- No system backend support

### macOS
- Supported: Metal
- The Metal-enabled llama.cpp build provides both accelerated and CPU-only execution
- A separate `llamacpp:cpu` selection is not required
