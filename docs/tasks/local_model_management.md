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

## Implementation Order

| Order | Phase | Task | Estimated Effort | Dependencies |
|-------|-------|------|-----------------|--------------|
| 1 | 1.3 | Extend `ModelInfo` structs (variants, hardware reqs) | Small | None |
| 2 | 1.1 | Extend JSON schema (variants, hardware, context scaling) | Small | 1 |
| 3 | 1.2 | Flesh out `ConfigDownloader` (clone/pull `arbiterAI_config`) | Medium | 2 |
| 4 | 2.1/2.2 | `HardwareDetector` (NVML + Vulkan via `dlopen`) | Medium | None |
| 5 | 2.3 | Model fit calculation (multi-GPU aware) | Small | 1, 4 |
| 6 | 6.1/6.2 | Docker GPU support (Dockerfile + runDocker.sh) | Small | None |
| 7 | — | Re-enable llama.cpp in CMake | Small | None |
| 8 | 3.1/3.2 | `ModelRuntime` refactor (multi-model, multi-GPU) | Large | 4, 5, 7 |
| 9 | 3.3/3.4 | Ready model strategy + swap queueing | Medium | 8 |
| 10 | 3.5 | Public API additions (load/unload/pin/switch) | Medium | 8 |
| 11 | 4.1 | `TelemetryCollector` | Medium | 8 |
| 12 | 4.2/4.3 | Instrument inference + library telemetry API | Small | 8, 11 |
| 13 | 5.1–5.4 | Standalone server (routes, dashboard, config) | Large | 10, 12 |

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
│   │   ├── configDownloader.h/cpp  # MODIFIED — full git clone/pull implementation
│   │   ├── arbiterAI.h/cpp         # MODIFIED — model management + telemetry API
│   │   ├── chatClient.h/cpp        # MODIFIED — switchModel()
│   │   └── providers/
│   │       ├── llamaInterface.h/cpp# MAJOR REFACTOR → ModelRuntime
│   │       └── llama.h/cpp         # MODIFIED — delegate to ModelRuntime
│   └── server/                     # NEW — standalone server application
│       ├── main.cpp                # Server entry point
│       ├── routes.h/cpp            # HTTP route handlers
│       └── dashboard.h             # Embedded HTML/JS dashboard
├── schemas/
│   └── model_config.schema.json    # MODIFIED — variants, hardware reqs
├── docker/
│   └── Dockerfile                  # MODIFIED — CUDA + Vulkan
├── runDocker.sh                    # MODIFIED — --gpus all
└── .gitignore                      # MODIFIED — arbiterAI_config/
```
