#ifndef _arbiterAI_modelManager_h_
#define _arbiterAI_modelManager_h_

#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

#include "configDownloader.h"

namespace arbiterAI
{

class ModelManager; // Forward declaration

struct DownloadMetadata {
    std::string url;
    std::string sha256;
    std::string cachePath;
};

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

/// Runtime options that control llama.cpp model loading and inference behavior.
/// These can be set per-model in the config and overridden at load time via the API.
struct RuntimeOptions {
    std::optional<bool> flashAttn;              // -fa: enable/disable flash attention
    std::optional<std::string> kvCacheTypeK;    // -ctk: KV cache type for keys (e.g. "f16", "q8_0", "q4_0")
    std::optional<std::string> kvCacheTypeV;    // -ctv: KV cache type for values
    std::optional<bool> noMmap;                 // --no-mmap: disable memory mapping
    std::optional<int> reasoningBudget;         // --reasoning-budget: reasoning token budget (0=disabled)
    std::optional<bool> swaFull;                // --swa-full: full SWA (sliding window attention)
    std::optional<int> nGpuLayers;              // -ngl: number of GPU layers (99=all)
    std::optional<std::string> overrideTensor;  // -ot: tensor override pattern (e.g. "per_layer_token_embd.weight=CPU")

    /// Merge another set of options on top of this one (override only non-empty fields).
    void mergeFrom(const RuntimeOptions &other);
};

struct ModelVariant {
    std::string quantization;
    int fileSizeMb=0;
    int minVramMb=0;
    int recommendedVramMb=0;
    VariantDownload download; // Primary / single-file download (backward compat)
    std::vector<VariantDownload> files; // All shard files (split GGUF). Empty = use download field.

    /// Get the complete list of files to download for this variant.
    /// Returns files if non-empty, otherwise a 1-element vector from download (if non-empty).
    std::vector<VariantDownload> getAllFiles() const;

    /// Get the primary filename (first shard / single file) used as the llama.cpp load path.
    /// Returns empty string if no download info is configured.
    std::string getPrimaryFilename() const;

    /// Check whether this variant is a split (multi-file) GGUF.
    bool isSplit() const;
};

struct Pricing
{
    double prompt_token_cost=0.0;
    double completion_token_cost=0.0;
};

inline void to_json(nlohmann::json &j, const Pricing &p)
{
    j=nlohmann::json{
        {"prompt_token_cost", p.prompt_token_cost},
        {"completion_token_cost", p.completion_token_cost}
    };
}

inline void from_json(const nlohmann::json &j, Pricing &p)
{
    j.at("prompt_token_cost").get_to(p.prompt_token_cost);
    j.at("completion_token_cost").get_to(p.completion_token_cost);
}

struct ModelInfo
{
    std::string model;
    std::string provider;
    std::string mode{ "chat" };
    std::string configVersion{ "1.1.0" }; // Current schema version
    std::string minSchemaVersion{ "1.0.0" }; // Minimum compatible schema version
    int ranking{ 50 }; // Default ranking (0-100)
    std::optional<std::string> apiBase;
    std::optional<std::string> filePath;
    std::optional<std::string> apiKey;
    std::optional<DownloadMetadata> download;
    std::optional<std::string> minClientVersion;
    std::optional<std::string> maxClientVersion;
    bool examplesAsSysMsg{ false };
    int contextWindow{ 4096 };
    int maxTokens{ 2048 };
    int maxInputTokens{ 3072 };
    int maxOutputTokens{ 1024 };
    Pricing pricing;
    std::optional<HardwareRequirements> hardwareRequirements;
    std::optional<ContextScaling> contextScaling;
    std::vector<ModelVariant> variants;
    RuntimeOptions runtimeOptions;              // Per-model llama.cpp runtime options
    std::vector<std::string> backendPriority;   // Ordered preference: ["vulkan", "rocm", "cuda"]
    std::vector<std::string> disabledBackends;  // Backends to exclude (model-level override)

    bool isCompatible(const std::string &clientVersion) const;
    bool isSchemaCompatible(const std::string &schemaVersion) const;
};

/// GPU architecture backend configuration entry.
/// Matched against detected GPU names to determine default backend behavior.
struct GpuBackendRule {
    std::string name;                           // Human-readable name (e.g. "AMD RDNA 3.5 (Strix Point)")
    std::vector<std::string> match;             // Case-insensitive substrings to match against GPU name
    std::vector<std::string> disabledBackends;  // Backends to disable for this architecture
    std::vector<std::string> backendPriority;   // Preferred backend order
    std::string notes;                          // Human-readable notes
};

class ModelManager
{
public:
    static ModelManager &instance();
    static void reset(); // For testing

    bool initialize(const std::vector<std::filesystem::path> &configPaths={}, const std::filesystem::path &localOverridePath="");
    std::optional<std::string> getProvider(const std::string &model) const;
    std::optional<ModelInfo> getModelInfo(const std::string &model) const;
    std::vector<ModelInfo> getModels(const std::string &provider) const;
    std::vector<ModelInfo> getModelsByRanking() const;
    void addModel(const ModelInfo &modelInfo);
    const std::map<std::string, std::string> &getModelProviderMap() const { return m_modelProviderMap; }

    // Runtime model config injection
    bool addModelFromJson(const nlohmann::json &modelJson, std::string &error);
    bool updateModelFromJson(const nlohmann::json &modelJson, std::string &error);
    bool removeModel(const std::string &modelName);
    static nlohmann::json modelInfoToJson(const ModelInfo &info);
    bool saveOverrides(const std::filesystem::path &overridePath) const;

    /// Set the directory where injected model configs are persisted as individual
    /// JSON files. Each file is named after the model (sanitized). On add/update
    /// the file is written; on delete the file is removed. Call loadInjectedConfigs()
    /// after initialize() to restore previously injected models.
    void setInjectedConfigDir(const std::filesystem::path &dir);

    /// Load all previously persisted injected model configs from the injected
    /// config directory. Models that already exist (from the config repo) are
    /// skipped — injected configs never shadow repo configs.
    int loadInjectedConfigs();

    /// Find the first GpuBackendRule whose match patterns hit the given GPU name.
    /// Returns nullopt if no rule matches.
    std::optional<GpuBackendRule> findGpuBackendRule(const std::string &gpuName) const;

    /// Get all loaded GPU backend rules (for diagnostics / API).
    const std::vector<GpuBackendRule> &getGpuBackendRules() const { return m_gpuBackendRules; }

public:
    static int compareVersions(const std::string &v1, const std::string &v2);

private:
    ModelManager()=default;
    bool loadModelFile(const std::filesystem::path &filePath);
    bool parseModelInfo(const nlohmann::json &jsonData, ModelInfo &info) const;
    bool validateSchema(const nlohmann::json &config) const;
    bool validateModelJson(const nlohmann::json &modelJson, std::string &error) const;
    void mergeModelInfo(ModelInfo &existing, const ModelInfo &source, const nlohmann::json &sourceJson) const;

    /// Persist a single injected model config to the injected config directory.
    bool saveInjectedConfig(const std::string &modelName) const;

    /// Remove a single injected model config file from the injected config directory.
    bool removeInjectedConfig(const std::string &modelName) const;

    /// Sanitize a model name into a safe filename (alphanumeric, hyphens, underscores).
    static std::string sanitizeFilename(const std::string &name);

    std::vector<ModelInfo> m_models;
    std::map<std::string, std::string> m_modelProviderMap;
    std::set<std::string> m_runtimeModels;
    std::vector<GpuBackendRule> m_gpuBackendRules;
    ConfigDownloader m_configDownloader;
    std::filesystem::path m_injectedConfigDir;
    bool m_initialized{ false };

    bool loadGpuBackendRules(const std::filesystem::path &filePath);
};

} // namespace arbiterAI

#endif//_arbiterAI_modelManager_h_
