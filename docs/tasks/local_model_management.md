# Task: Local Model Management (Llama.cpp)

Flesh out the llama.cpp provider with comprehensive local model management capabilities.

## Goals

1. **Central Config Repo** — Pull model definitions from [`caseymcc/arbiterAI_config`](https://github.com/caseymcc/arbiterAI_config), listing available local models with hardware requirements, download URLs, and quantization variants.
2. **Hardware Discovery** — Detect available accelerators (NVIDIA via NVML, Vulkan) and determine which models can run and at what context sizes. Support multi-GPU tensor splitting.
3. **Runtime Model Swapping** — Load/unload models on demand, keep recently used or pinned models "ready" in system memory or VRAM for quick reload. Queue swap requests during active inference.
4. **Performance Telemetry** — Collect tokens/sec, memory usage (system + VRAM), CPU/GPU utilization, swap times. Expose telemetry data via library API.
5. **Standalone Server** — A separate server application that uses the ArbiterAI library to serve an OpenAI-compatible API, model management endpoints, and a live stats dashboard. The library collects telemetry; the server handles presentation and external communication.

## Design Decisions

The following decisions were resolved during planning:

| # | Question | Decision |
|---|----------|----------|
| 1 | Multi-GPU support | **Yes.** Support tensor splitting across multiple GPUs via llama.cpp's built-in split support. |
| 2 | GPU backends | **CUDA/NVML and Vulkan from day one.** Keep the `HardwareDetector` interface abstract so future backends (ROCm, Metal) slot in easily. |
| 3 | Dashboard scope | **Standalone server.** The library itself only collects telemetry and exposes it via API. A separate server application (new build target) handles the HTTP dashboard, external client communication, prompt requests, and model management. This keeps `cpp-httplib` out of the core library. |
| 4 | Config repo | **Use [`caseymcc/arbiterAI_config`](https://github.com/caseymcc/arbiterAI_config).** Clone into a local `arbiterAI_config/` directory within the project root (added to `.gitignore`). Pull from `main` by default, support pinning to a tag/version. |
| 5 | Ready model RAM budget | **Configurable, default 50% of system RAM.** If a model fits in VRAM alongside already-live models, leave it loaded in VRAM rather than evicting to RAM. |
| 6 | Swap during inference | **Queue the swap.** Finish the current in-flight inference request, then perform the swap. Return a "queued" status to the caller. |
| 7 | Docker GPU passthrough | **Update existing Docker files.** Add NVIDIA Container Toolkit support (`--gpus all`) to `runDocker.sh` and CUDA toolkit to `Dockerfile`. Test device: RTX 3060. |
| 8 | Quantization variants | **Single model entry with multiple variants.** Each model has a `variants` array; each variant specifies its quantization, file size, download URL, SHA256, and hardware requirements. |

---

## Phase 1: Model Config Repository & Schema

**Goal:** Define what info we need per local model and how it gets to the client.

### 1.1 Extend Model Config Schema

Add local-model-specific fields to `schemas/model_config.schema.json`. Models with local providers use a `variants` array — each variant represents a different quantization of the same model:

```jsonc
{
  // existing fields (name, provider, ranking, etc.)...
  "hardware_requirements": {
    "min_system_ram_mb": 8192,    // minimum system RAM (CPU offload)
    "parameter_count": "7B"       // human-readable param count
  },
  "context_scaling": {
    "base_context": 4096,         // context at min VRAM
    "max_context": 131072,        // maximum context the model supports
    "vram_per_1k_context_mb": 64  // additional VRAM per 1K context tokens
  },
  "variants": [
    {
      "quantization": "Q4_K_M",
      "file_size_mb": 4370,
      "min_vram_mb": 4096,
      "recommended_vram_mb": 8192,
      "download": {
        "url": "https://huggingface.co/...",
        "sha256": "abc123...",
        "filename": "model-q4_k_m.gguf"
      }
    },
    {
      "quantization": "Q8_0",
      "file_size_mb": 8100,
      "min_vram_mb": 8192,
      "recommended_vram_mb": 12288,
      "download": {
        "url": "https://huggingface.co/...",
        "sha256": "def456...",
        "filename": "model-q8_0.gguf"
      }
    }
  ]
}
```

### 1.2 Central Config Repository

- Use the existing [`caseymcc/arbiterAI_config`](https://github.com/caseymcc/arbiterAI_config) repository for model definitions.
- `ConfigDownloader` already has skeleton code for cloning a git repo — flesh it out to:
  - Clone/pull on `initialize()` with a configurable refresh interval.
  - Clone into `arbiterAI_config/` at the project root (added to `.gitignore`).
  - Fall back to local cached copy if network is unavailable.
  - Support a version/tag parameter to pin to a known-good config set. Default: pull from `main`.
- `ModelManager` merges remote configs with local overrides (already has layered loading).

### 1.3 Update `ModelInfo` Struct

Add new fields to `ModelInfo` in `modelManager.h`:

```cpp
struct HardwareRequirements {
    int minSystemRamMb=0;
    std::string parameterCount;
};

struct ContextScaling {
    int baseContext=4096;
    int maxContext=4096;
    int vramPer1kContextMb=0;
};

struct VariantDownload {
    std::string url;
    std::string sha256;
    std::string filename;
};

struct ModelVariant {
    std::string quantization;
    int fileSizeMb=0;
    int minVramMb=0;
    int recommendedVramMb=0;
    VariantDownload download;
};
```

Add to `ModelInfo`:
```cpp
std::optional<HardwareRequirements> hardwareRequirements;
std::optional<ContextScaling> contextScaling;
std::vector<ModelVariant> variants; // empty for cloud providers
```

### Files affected
- `schemas/model_config.schema.json`
- `src/arbiterAI/modelManager.h` — new structs + fields on `ModelInfo`
- `src/arbiterAI/modelManager.cpp` — parsing logic for variants
- `src/arbiterAI/configDownloader.h/cpp` — flesh out git clone/pull
- `.gitignore` — add `arbiterAI_config/`

---

## Phase 2: Hardware Discovery

**Goal:** Know what's available on the machine so we can match models to hardware.

### 2.1 New Component: `HardwareDetector`

Create `src/arbiterAI/hardwareDetector.h/cpp`:

```cpp
enum class GpuBackend {
    None,
    CUDA,
    Vulkan
};

struct GpuInfo {
    int index;
    std::string name;
    GpuBackend backend;
    int vramTotalMb;
    int vramFreeMb;
    float computeCapability;    // CUDA only, 0.0 for Vulkan
    float utilizationPercent;
};

struct SystemInfo {
    int totalRamMb;
    int freeRamMb;
    int cpuCores;
    float cpuUtilizationPercent;
    std::vector<GpuInfo> gpus;
};

class HardwareDetector {
public:
    static HardwareDetector &instance();

    /// Refresh hardware info (call periodically or on demand)
    void refresh();

    SystemInfo getSystemInfo() const;
    std::vector<GpuInfo> getGpus() const;
    int getTotalFreeVramMb() const;
    int getTotalFreeRamMb() const;
};
```

### 2.2 Implementation Strategy

- **NVIDIA GPUs**: Use NVML (`libnvidia-ml.so`) loaded at runtime via `dlopen` — not a hard link-time dependency. Query `nvmlDeviceGetMemoryInfo`, `nvmlDeviceGetUtilizationRates`, `nvmlDeviceGetName`, `nvmlDeviceGetCudaComputeCapability`.
- **Vulkan GPUs**: Use Vulkan API (`libvulkan.so`) loaded at runtime via `dlopen`. Query `vkEnumeratePhysicalDevices`, `vkGetPhysicalDeviceProperties`, `vkGetPhysicalDeviceMemoryProperties`. Provides device name, VRAM totals. Utilization may require vendor extensions.
- **System RAM**: `/proc/meminfo` on Linux, `GlobalMemoryStatusEx` on Windows.
- **CPU**: `/proc/stat` on Linux, `GetSystemTimes` on Windows.
- **Future backends**: ROCm/HIP for AMD GPUs, Metal for macOS — the abstract `GpuBackend` enum and `GpuInfo` struct make these easy to add.
- **Multi-GPU**: Enumerate all detected GPUs. `ModelFit` calculation aggregates VRAM across GPUs when tensor splitting is viable.

### 2.3 Model Compatibility Calculation

Add to `ModelManager` or a new `ModelFitCalculator`:

```cpp
struct ModelFit {
    std::string model;
    std::string variant;            // selected quantization variant
    bool canRun;                    // meets minimum requirements
    int maxContextSize;             // maximum context given available VRAM
    std::string limitingFactor;     // "vram", "ram", "compute_capability"
    int estimatedVramUsageMb;       // at maxContextSize
    std::vector<int> gpuIndices;    // which GPUs to use (multi-GPU splitting)
};

std::vector<ModelFit> calculateFittableModels(const SystemInfo &hw);
ModelFit calculateModelFit(const ModelInfo &model, const ModelVariant &variant, const SystemInfo &hw);
```

When multiple GPUs are available, the calculator should consider tensor splitting across GPUs and report which GPUs would be used.

### Files affected
- `src/arbiterAI/hardwareDetector.h/cpp` — **new**
- `src/arbiterAI/modelManager.h/cpp` — model fit calculation
- `CMakeLists.txt` — optional NVML/Vulkan runtime linkage

---

## Phase 3: Runtime Model Swapping & Readiness

**Goal:** Load/unload/swap models dynamically. Keep select models "ready" for fast reload.

### 3.1 Refactor `LlamaInterface` → `ModelRuntime`

The current `LlamaInterface` is a singleton that holds one model. Refactor to manage multiple model slots:

```cpp
enum class ModelState {
    Unloaded,       // not in memory
    Downloading,    // download in progress
    Ready,          // in system RAM (offloaded from VRAM, quick to reload)
    Loaded,         // fully loaded in VRAM, ready for inference
    Unloading       // being evicted
};

struct LoadedModel {
    std::string modelName;
    std::string variant;          // quantization variant in use
    ModelState state;
    llama_model *model=nullptr;
    llama_context *ctx=nullptr;
    int vramUsageMb=0;
    int ramUsageMb=0;
    int contextSize=0;
    std::vector<int> gpuIndices;  // GPUs this model is split across
    std::chrono::steady_clock::time_point lastUsed;
    bool pinned=false;            // user requested "keep ready"
};
```

### 3.2 Model Lifecycle Manager

```cpp
class ModelRuntime {
public:
    /// Load a model into VRAM for inference
    ErrorCode loadModel(const std::string &model, const std::string &variant="", int contextSize=0);

    /// Unload a model (move to Ready if pinned, Unloaded otherwise)
    ErrorCode unloadModel(const std::string &model);

    /// Pin/unpin: keep model in RAM for quick reload after eviction
    ErrorCode pinModel(const std::string &model);
    ErrorCode unpinModel(const std::string &model);

    /// Swap: unload current, load new — queued if inference is in-flight
    ErrorCode swapModel(const std::string &newModel, const std::string &variant="", int contextSize=0);

    /// Get state of all models
    std::vector<LoadedModel> getModelStates() const;

    /// Eviction: free VRAM/RAM by unloading least-recently-used non-pinned models
    void evictIfNeeded(int requiredVramMb);

private:
    std::map<std::string, LoadedModel> m_models;
    mutable std::mutex m_mutex;
    int m_readyRamBudgetMb;       // configurable, default 50% of system RAM
    std::atomic<bool> m_inferenceActive{false};
    std::queue<std::function<void()>> m_pendingSwaps;
};
```

### 3.3 Swap Queueing

When `swapModel()` is called while `m_inferenceActive` is true:
1. The swap request is pushed to `m_pendingSwaps`.
2. A "queued" status is returned to the caller.
3. When the active inference completes, the runtime drains the swap queue (executing the most recent swap, discarding stale ones).
4. If no inference is active, the swap executes immediately.

### 3.4 "Ready" Model Strategy

- **RAM budget**: Configurable via `ModelRuntime::setReadyRamBudget(int mb)`. Default: 50% of total system RAM (queried from `HardwareDetector`).
- **VRAM preference**: If a model fits in VRAM alongside already-loaded models, it stays loaded in VRAM rather than being demoted to "Ready" in RAM.
- **Pinned models**: When unloaded from VRAM, keep `llama_model*` in system RAM (if within budget). Context is freed, but model weights stay resident. Reload to VRAM is much faster than re-reading from disk.
- **LRU eviction**: Non-pinned models in the "Ready" tier are evicted LRU-first when the RAM budget is exceeded.
- **Multi-GPU aware**: When loading a model, `ModelRuntime` consults `HardwareDetector` and `ModelFit` to determine optimal GPU placement and tensor split ratios.

### 3.5 Public API Additions

Add to `ArbiterAI` and/or `ChatClient`:

```cpp
// On ArbiterAI (global)
ErrorCode loadModel(const std::string &model, const std::string &variant="", int contextSize=0);
ErrorCode unloadModel(const std::string &model);
ErrorCode pinModel(const std::string &model);
ErrorCode unpinModel(const std::string &model);
std::vector<ModelFit> getLocalModelCapabilities();
std::vector<LoadedModel> getLoadedModels();

// On ChatClient — when switching models mid-session
ErrorCode switchModel(const std::string &newModel);
```

### Files affected
- `src/arbiterAI/providers/llamaInterface.h/cpp` — major refactor → `ModelRuntime`
- `src/arbiterAI/providers/llama.h/cpp` — delegate to `ModelRuntime`
- `src/arbiterAI/arbiterAI.h/cpp` — new public methods
- `src/arbiterAI/chatClient.h/cpp` — `switchModel()`

---

## Phase 4: Performance Telemetry

**Goal:** Collect runtime stats within the library. Presentation is handled by the standalone server (Phase 5).

### 4.1 New Component: `TelemetryCollector`

```cpp
struct InferenceStats {
    std::string model;
    std::string variant;
    double tokensPerSecond;
    int promptTokens;
    int completionTokens;
    double latencyMs;                 // time to first token
    double totalTimeMs;               // total request time
    std::chrono::system_clock::time_point timestamp;
};

struct SystemSnapshot {
    SystemInfo hardware;              // from HardwareDetector
    std::vector<LoadedModel> models;  // from ModelRuntime
    double avgTokensPerSecond;        // rolling average
    int activeRequests;
};

class TelemetryCollector {
public:
    static TelemetryCollector &instance();

    /// Record an inference event
    void recordInference(const InferenceStats &stats);

    /// Record a model swap event
    void recordModelSwap(const std::string &from, const std::string &to, double swapTimeMs);

    /// Get current snapshot
    SystemSnapshot getSnapshot() const;

    /// Get inference history (last N minutes)
    std::vector<InferenceStats> getHistory(std::chrono::minutes window) const;

    /// Get model swap history
    struct SwapEvent {
        std::string from;
        std::string to;
        double timeMs;
        std::chrono::system_clock::time_point when;
    };
    std::vector<SwapEvent> getSwapHistory() const;
};
```

### 4.2 Instrument Inference Path

In `ModelRuntime::completion()` / `streamingCompletion()`:
- Record start time, first-token time, end time.
- Calculate tokens/sec from token count and elapsed time.
- Query `HardwareDetector` for current memory/utilization.
- Push to `TelemetryCollector`.
- Set `m_inferenceActive` flag; clear on completion and drain pending swaps.

### 4.3 Library Telemetry API

Expose telemetry through `ArbiterAI`:

```cpp
// On ArbiterAI
SystemSnapshot getTelemetrySnapshot() const;
std::vector<InferenceStats> getInferenceHistory(std::chrono::minutes window) const;
```

This keeps the library independent of any HTTP framework. The standalone server (Phase 5) queries these APIs and serves them over HTTP.

### Files affected
- `src/arbiterAI/telemetryCollector.h/cpp` — **new**
- `src/arbiterAI/hardwareDetector.h/cpp` — periodic sampling
- `src/arbiterAI/providers/llamaInterface.cpp` (→ `ModelRuntime`) — instrumentation
- `src/arbiterAI/arbiterAI.h/cpp` — telemetry API surface

---

## Phase 5: Standalone Server

**Goal:** A separate application (not part of the core library) that provides HTTP endpoints for chat completions, model management, telemetry, and a live dashboard.

### 5.1 Architecture

The standalone server is a new CMake target (`arbiterAI-server`) that links against the `arbiterai` library. It uses `cpp-httplib` for HTTP serving — this keeps `cpp-httplib` as a dependency of the server application only, not the core library.

```
┌──────────────────────────────────────────────────┐
│                arbiterAI-server                  │
│                                                  │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐ │
│  │  Chat API  │  │ Model Mgmt │  │ Dashboard  │ │
│  │ /v1/chat/* │  │ /api/model*│  │ /dashboard │ │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘ │
│        │               │               │        │
│  ┌─────┴───────────────┴───────────────┴──────┐  │
│  │              cpp-httplib                    │  │
│  └─────────────────┬──────────────────────────┘  │
└────────────────────┼─────────────────────────────┘
                     │ links
┌────────────────────┼─────────────────────────────┐
│              arbiterai (library)                  │
│                                                  │
│  ArbiterAI ─── ChatClient ─── BaseProvider       │
│  ModelManager   HardwareDetector   ModelRuntime   │
│  TelemetryCollector   CostManager   CacheManager  │
└──────────────────────────────────────────────────┘
```

### 5.2 REST API Endpoints

The server consolidates the existing proxy functionality with new model management and telemetry endpoints:

**Chat Completions (OpenAI-compatible):**

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/v1/chat/completions` | POST | Chat completion (streaming + non-streaming) |
| `/v1/models` | GET | List available models |

**Model Management:**

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/models` | GET | Available models + hardware fit info |
| `/api/models/loaded` | GET | Currently loaded models + state |
| `/api/models/{name}/load` | POST | Load a model, `?variant=Q4_K_M&context=8192` |
| `/api/models/{name}/unload` | POST | Unload a model |
| `/api/models/{name}/pin` | POST | Pin model for quick reload |
| `/api/models/{name}/unpin` | POST | Unpin model |
| `/api/models/{name}/download` | POST | Download a model variant |
| `/api/models/{name}/download` | GET | Get download progress |

**Telemetry:**

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/stats` | GET | Current system snapshot (JSON) |
| `/api/stats/history` | GET | Inference history (JSON), `?minutes=N` |
| `/api/stats/swaps` | GET | Model swap history |
| `/api/hardware` | GET | Current hardware info (GPUs, RAM, CPU) |

**Dashboard:**

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/dashboard` | GET | HTML dashboard page |

### 5.3 Dashboard Page

- Single-page HTML with embedded JS (served as a static string from C++, no external file dependencies).
- Live-updating charts: tokens/sec over time, VRAM/RAM usage, GPU utilization.
- Model table: loaded models, state, variant, context size, GPU assignment, last used.
- System info: GPU names, VRAM total/used, RAM total/used, CPU cores.
- Model management: load/unload/pin buttons, download progress bars.
- Use lightweight charting (inline SVG or a small embedded chart library).

### 5.4 Server Configuration

The server accepts command-line arguments (via `cxxopts`) and/or a config file:

```
arbiterAI-server [options]
  --port, -p        HTTP port (default: 8080)
  --host, -h        Bind address (default: 0.0.0.0)
  --config, -c      Model config path(s)
  --model, -m       Default model to load on startup
  --variant, -v     Default variant (e.g., Q4_K_M)
  --ram-budget      Ready model RAM budget in MB (default: 50% of system RAM)
  --log-level       Log level (default: info)
```

### 5.5 Replaces Existing Proxy

The standalone server supersedes the current `examples/proxy/` example. The proxy example can be kept as a minimal reference, but the server is the production-ready application.

### Files affected
- `src/server/main.cpp` — **new** (server entry point)
- `src/server/routes.h/cpp` — **new** (route handlers)
- `src/server/dashboard.h` — **new** (embedded HTML/JS as string literal)
- `CMakeLists.txt` — new `arbiterAI-server` target
- `docs/` — server documentation

---

## Phase 6: Docker GPU Support

**Goal:** Update the Docker environment to support GPU passthrough for local model inference.

### 6.1 Dockerfile Updates

Add NVIDIA CUDA toolkit and Vulkan SDK to `docker/Dockerfile`:

```dockerfile
# Add NVIDIA CUDA toolkit
RUN apt-get update && apt-get install -y \
    nvidia-cuda-toolkit \
    vulkan-tools \
    libvulkan-dev \
    mesa-vulkan-drivers
```

### 6.2 runDocker.sh Updates

Add `--gpus all` flag to the `docker run` command. Detect whether NVIDIA Container Toolkit is available and add the flag conditionally:

```bash
# Detect GPU support
GPU_FLAG=""
if command -v nvidia-smi &> /dev/null && docker info 2>/dev/null | grep -q "nvidia"; then
    GPU_FLAG="--gpus all"
fi

docker run -d -it $GPU_FLAG --name $CONTAINER_NAME ...
```

### 6.3 Test Environment

- Primary test device: NVIDIA RTX 3060 (12 GB VRAM, Compute Capability 8.6).
- The GPU flag is optional — builds and tests still work without a GPU, but llama.cpp inference falls back to CPU-only.

### Files affected
- `docker/Dockerfile` — CUDA + Vulkan packages
- `runDocker.sh` — conditional `--gpus all` flag

---

## Phase 7: Llama Provider Refactor & Re-enable

**Goal:** Delete the legacy `LlamaInterface` singleton, move llama.cpp model ownership (`llama_model*` / `llama_context*`) into `ModelRuntime`, rewrite the `Llama` provider to delegate to `ModelRuntime` for inference, add proper chat template formatting, re-enable the llama.cpp build, and verify end-to-end with a Qwen2.5-7B-Instruct Q4_K_M test.

### Current State

The llama provider is fully disabled:
- `CMakeLists.txt` — llama source files, `find_package(llama)`, and `llama` link target are all commented out.
- `vcpkg.json` — `llama-cpp` is not listed as a dependency (custom port exists at `vcpkg/custom_ports/llama-cpp/`).
- `arbiterAI.cpp` — `#include "arbiterAI/providers/llama.h"` and the `createProvider("llama")` case are commented out.
- `LlamaInterface` — Pre-Phase-3 singleton that manages one model at a time with its own download logic, duplicating responsibilities now handled by `ModelRuntime`.

### 7.1 Re-enable llama.cpp Build

Add the llama.cpp dependency back into the build system:

**`vcpkg.json`:**
```json
"dependencies": [
    // ...existing deps...
    "llama-cpp"
]
```

**`CMakeLists.txt`:**
- Uncomment `find_package(llama CONFIG REQUIRED)`
- Add llama source files back to `arbiterai_src` (new files, not the old `llamaInterface` ones)
- Uncomment `${LLAMA_INCLUDE_DIRS}` in `target_include_directories`
- Uncomment `llama` in `target_link_libraries`

### 7.2 Move llama.cpp Handles into `ModelRuntime`

`ModelRuntime` already tracks model state (`LoadedModel` struct with state, variant, context size, etc.) but doesn't own any llama.cpp objects. Extend it to own the actual model/context handles for local models.

**Add to `LoadedModel`:**
```cpp
struct LoadedModel {
    // ...existing fields...
    llama_model *llamaModel=nullptr;
    llama_context *llamaCtx=nullptr;
};
```

**Add to `ModelRuntime`:**
```cpp
class ModelRuntime {
public:
    // ...existing public methods...

    /// Get the llama_model handle for a loaded local model.
    /// Returns nullptr if not loaded or not a local model.
    llama_model *getLlamaModel(const std::string &model) const;

    /// Get the llama_context handle for a loaded local model.
    /// Returns nullptr if not loaded or not a local model.
    llama_context *getLlamaContext(const std::string &model) const;

    /// Get the ModelInfo for a loaded model (for maxOutputTokens, etc.).
    std::optional<ModelInfo> getLoadedModelInfo(const std::string &model) const;

private:
    // ...existing private members...
    bool m_llamaInitialized=false;

    /// Initialize the llama.cpp backend (called once on first local model load).
    void initLlamaBackend();

    /// Actually load a GGUF file into llama.cpp.
    ErrorCode loadLlamaModel(const std::string &model, const std::string &filePath,
        int contextSize, const std::vector<int> &gpuIndices);

    /// Free llama.cpp resources for a model.
    void freeLlamaModel(LoadedModel &entry);
};
```

The key changes to `ModelRuntime::loadModel()`:
1. After hardware-fit checks and download verification, call `loadLlamaModel()` which calls `llama_model_load_from_file()` and `llama_init_from_model()`.
2. Store the `llama_model*` and `llama_context*` in the `LoadedModel` entry.
3. Set GPU layers via `llama_model_default_params().n_gpu_layers` based on `gpuIndices`.

The key changes to `ModelRuntime::unloadModel()`:
1. Call `freeLlamaModel()` to `llama_free()` / `llama_model_free()` the handles.
2. For pinned → Ready transition: free the context but keep the model weights.

### 7.3 Delete `LlamaInterface`

Remove `llamaInterface.h` and `llamaInterface.cpp` entirely. All their responsibilities are absorbed:

| `LlamaInterface` Responsibility | New Owner |
|---------------------------------|-----------|
| `llama_backend_init/free` | `ModelRuntime` (init on first local load, free in destructor) |
| `llama_model_load_from_file` / `llama_model_free` | `ModelRuntime::loadLlamaModel()` / `freeLlamaModel()` |
| `llama_init_from_model` / `llama_free` | `ModelRuntime::loadLlamaModel()` / `freeLlamaModel()` |
| `completion()` / `streamingCompletion()` | `Llama` provider (using handles from `ModelRuntime`) |
| `getEmbeddings()` | `Llama` provider (using handles from `ModelRuntime`) |
| `loadModel()` / `isLoaded()` | `ModelRuntime::loadModel()` / `getModelState()` |
| `setModels()` | Removed — `ModelManager` already tracks all model configs |
| `downloadModel()` / `getDownloadStatus()` | `ModelRuntime` (already has download tracking) |
| `getAvailableModels()` | `ModelManager::getModelsByProvider("llama")` |

### 7.4 Rewrite `Llama` Provider

The `Llama` provider becomes a thin wrapper that:
1. Gets llama.cpp handles from `ModelRuntime` (instead of `LlamaInterface`).
2. Performs tokenization, sampling, and decoding directly.
3. Applies proper chat template formatting.
4. Records telemetry via `TelemetryCollector`.

```cpp
class Llama : public BaseProvider {
public:
    Llama();
    ~Llama();

    ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;

    DownloadStatus getDownloadStatus(const std::string &modelName,
        std::string &error) override;

    ErrorCode getAvailableModels(std::vector<std::string> &models) override;

private:
    /// Format messages into a prompt string using the model's chat template.
    std::string applyTemplate(llama_model *model,
        const std::vector<Message> &messages) const;

    /// Run the inference loop (shared by completion and streaming).
    ErrorCode runInference(llama_model *model, llama_context *ctx,
        const CompletionRequest &request, const ModelInfo &modelInfo,
        std::string &result,
        std::function<void(const std::string &)> streamCallback);
};
```

**Key design points:**
- `completion()` calls `ModelRuntime::instance().loadModel()` if not loaded, then gets handles via `getLlamaModel()` / `getLlamaContext()`.
- Calls `ModelRuntime::beginInference()` / `endInference()` to protect against swap-during-inference.
- Records `InferenceStats` to `TelemetryCollector` after each completion.
- Uses `ModelRuntime::getLoadedModelInfo()` to get `maxOutputTokens`, `contextWindow`, etc.

### 7.5 Chat Template Support

The current `LlamaInterface` concatenates all message contents into a flat string, ignoring roles. This produces garbage output from instruction-tuned models like Qwen2.5.

**Solution:** Use llama.cpp's built-in `llama_chat_apply_template()` function. This reads the chat template from the GGUF file's metadata and formats messages correctly. For Qwen2.5, this produces ChatML format:

```
<|im_start|>system
{system_prompt}<|im_end|>
<|im_start|>user
{prompt}<|im_end|>
<|im_start|>assistant
```

Implementation in `Llama::applyTemplate()`:
```cpp
std::string Llama::applyTemplate(llama_model *model,
    const std::vector<Message> &messages) const
{
    // Build llama_chat_message array
    std::vector<llama_chat_message> chatMessages;
    for(const Message &msg:messages)
    {
        chatMessages.push_back({msg.role.c_str(), msg.content.c_str()});
    }

    // First call to get required buffer size
    int len=llama_chat_apply_template(
        llama_model_get_vocab(model),
        nullptr, // use model's built-in template
        chatMessages.data(), chatMessages.size(),
        true, // add generation prompt
        nullptr, 0);

    std::string result(len, '\0');

    // Second call to fill the buffer
    llama_chat_apply_template(
        llama_model_get_vocab(model),
        nullptr,
        chatMessages.data(), chatMessages.size(),
        true,
        result.data(), result.size());

    return result;
}
```

If `llama_chat_apply_template()` returns an error (template not found in GGUF metadata), fall back to a generic ChatML formatter.

### 7.6 Re-enable Provider Registration

**`arbiterAI.cpp`:**
```cpp
#include "arbiterAI/providers/llama.h"  // Uncomment

// In createProvider():
else if(provider=="llama")
{
    return std::make_unique<Llama>();
}
```

### 7.7 Update `arbiterAI_config`

The config repo entry at `arbiterAI_config/configs/defaults/models/llama.json` has already been updated (in the previous session) with Qwen2.5-7B-Instruct using the v1.1.0 schema with `variants`, `hardware_requirements`, and `context_scaling`. SHA256 hashes are placeholders — they must be computed from the actual downloaded files before the first real download test.

### 7.8 Test with Qwen2.5-7B-Instruct Q4_K_M

Add an integration test that verifies end-to-end llama.cpp inference. This test requires the GGUF file to be present on disk (skip if not available).

**Test model:** [bartowski/Qwen2.5-7B-Instruct-GGUF](https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF) — Q4_K_M variant (4.68 GB).

| Property | Value |
|----------|-------|
| Model | Qwen2.5-7B-Instruct |
| Quantization | Q4_K_M |
| File | `Qwen2.5-7B-Instruct-Q4_K_M.gguf` |
| File size | 4.68 GB |
| License | Apache 2.0 |
| Min VRAM | ~5 GB |
| Test device | NVIDIA RTX 3060 12 GB |
| Context | 4096 (conservative for testing) |

**Test file:** `tests/llamaProviderTests.cpp`

```cpp
// Skip if model file is not present
class LlamaProviderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        modelPath="/models/Qwen2.5-7B-Instruct-Q4_K_M.gguf";
        if(!std::filesystem::exists(modelPath))
        {
            GTEST_SKIP() << "Qwen2.5-7B-Instruct Q4_K_M not found at " << modelPath;
        }
        // Initialize ArbiterAI with config that includes the llama model
    }
};

TEST_F(LlamaProviderTest, BasicCompletion) { /* ... */ }
TEST_F(LlamaProviderTest, StreamingCompletion) { /* ... */ }
TEST_F(LlamaProviderTest, ChatTemplateApplied) { /* ... */ }
TEST_F(LlamaProviderTest, TokenUsageReported) { /* ... */ }
TEST_F(LlamaProviderTest, ModelRuntimeTracksState) { /* ... */ }
TEST_F(LlamaProviderTest, TelemetryRecorded) { /* ... */ }
```

Tests use `GTEST_SKIP()` when the model file is absent so CI passes without a GPU and without a 5 GB download.

### Files affected
- `vcpkg.json` — add `llama-cpp` dependency
- `CMakeLists.txt` — re-enable llama sources, `find_package`, link target; add test file
- `src/arbiterAI/providers/llamaInterface.h` — **DELETE**
- `src/arbiterAI/providers/llamaInterface.cpp` — **DELETE**
- `src/arbiterAI/providers/llama.h` — **REWRITE** — thin provider delegating to `ModelRuntime`
- `src/arbiterAI/providers/llama.cpp` — **REWRITE** — inference, chat template, telemetry
- `src/arbiterAI/modelRuntime.h` — add `llama_model*`/`llama_context*` to `LoadedModel`, add accessor/loader methods
- `src/arbiterAI/modelRuntime.cpp` — implement `loadLlamaModel()`, `freeLlamaModel()`, `initLlamaBackend()`
- `src/arbiterAI/arbiterAI.cpp` — uncomment llama include and `createProvider` case
- `tests/llamaProviderTests.cpp` — **NEW** — integration tests (skip if model absent)
- `arbiterAI_config/configs/defaults/models/llama.json` — already updated with Qwen2.5-7B-Instruct

---

## Implementation Order

| Order | Phase | Task | Estimated Effort | Dependencies |
|-------|-------|------|-----------------|--------------|
| 1 | 1.3 | Extend `ModelInfo` structs (variants, hardware reqs) | Small | None |
| 2 | 1.1 | Extend JSON schema (variants, hardware, context scaling) | Small | 1 |
| 3 | 1.2 | Flesh out `ConfigDownloader` (clone/pull `arbiterAI_config`) | Medium | 2 |
| 4 | 2.1/2.2 | `HardwareDetector` (NVML + Vulkan via `dlopen`) | Medium | None |
| 5 | 2.3 | Model fit calculation (multi-GPU aware) | Small | 1, 4 |
| 6 | 6.1/6.2 | Docker GPU support (Dockerfile + runDocker.sh) | Small | None |
| 7 | 3.1/3.2 | `ModelRuntime` (multi-model state tracking, swap queueing) | Large | 4, 5 |
| 8 | 3.3/3.4 | Ready model strategy + swap queueing | Medium | 7 |
| 9 | 3.5 | Public API additions (load/unload/pin/switch) | Medium | 7 |
| 10 | 4.1 | `TelemetryCollector` | Medium | 7 |
| 11 | 4.2/4.3 | Instrument inference + library telemetry API | Small | 7, 10 |
| 12 | 5.1–5.4 | Standalone server (routes, dashboard, config) | Large | 9, 11 |
| 13 | 7.1 | Re-enable llama.cpp build (`vcpkg.json`, `CMakeLists.txt`) | Small | None |
| 14 | 7.2/7.3 | Move llama.cpp handles into `ModelRuntime`, delete `LlamaInterface` | Large | 7, 13 |
| 15 | 7.4/7.5 | Rewrite `Llama` provider + chat template support | Medium | 14 |
| 16 | 7.6 | Re-enable provider registration in `arbiterAI.cpp` | Small | 15 |
| 17 | 7.7/7.8 | Update config, add Qwen2.5-7B-Instruct Q4_K_M tests | Medium | 15, 16 |

---

## Project Structure (New/Modified Files)

```
arbiterAI/
├── arbiterAI_config/               # ← cloned from caseymcc/arbiterAI_config (gitignored)
├── src/
│   ├── arbiterAI/
│   │   ├── hardwareDetector.h/cpp  # NEW — GPU/RAM/CPU detection
│   │   ├── telemetryCollector.h/cpp# NEW — inference stats collection
│   │   ├── modelManager.h/cpp      # MODIFIED — variants, hardware reqs, model fit
│   │   ├── modelRuntime.h/cpp      # MODIFIED — llama.cpp handle ownership
│   │   ├── configDownloader.h/cpp  # MODIFIED — full git clone/pull implementation
│   │   ├── arbiterAI.h/cpp         # MODIFIED — model management + telemetry API + llama registration
│   │   ├── chatClient.h/cpp        # MODIFIED — switchModel()
│   │   └── providers/
│   │       ├── llamaInterface.h/cpp# DELETE — absorbed by ModelRuntime + Llama provider
│   │       └── llama.h/cpp         # REWRITE — thin provider delegating to ModelRuntime
│   └── server/                     # NEW — standalone server application
│       ├── main.cpp                # Server entry point
│       ├── routes.h/cpp            # HTTP route handlers
│       └── dashboard.h             # Embedded HTML/JS dashboard
├── tests/
│   └── llamaProviderTests.cpp      # NEW — integration tests (skip if model absent)
├── schemas/
│   └── model_config.schema.json    # MODIFIED — variants, hardware reqs
├── docker/
│   └── Dockerfile                  # MODIFIED — CUDA + Vulkan
├── runDocker.sh                    # MODIFIED — --gpus all
└── .gitignore                      # MODIFIED — arbiterAI_config/
```
