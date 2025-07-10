#ifndef _hermesaxiom_modelManager_h_
#define _hermesaxiom_modelManager_h_

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

namespace hermesaxiom
{

class ModelManager; // Forward declaration

struct DownloadMetadata
{
    std::string url;
    std::string sha256;
    std::string cachePath;
};

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
    double inputCostPerToken{ 0.0 };
    double outputCostPerToken{ 0.0 };

    bool isCompatible(const std::string &clientVersion) const;
    bool isSchemaCompatible(const std::string &schemaVersion) const;
};

class ModelManager
{
public:
    static ModelManager &instance();
    static void reset(); // For testing

    bool initialize(const std::vector<std::filesystem::path> &configPaths, const std::vector<std::string> &remoteUrls={});
    std::optional<std::string> getProvider(const std::string &model) const;
    std::optional<ModelInfo> getModelInfo(const std::string &model) const;
    std::vector<ModelInfo> getModels(const std::string &provider) const;
    std::vector<ModelInfo> getModelsByRanking() const;
    void addModel(const ModelInfo &modelInfo);
    const std::map<std::string, std::string> &getModelProviderMap() const { return m_modelProviderMap; }

public:
    static int compareVersions(const std::string &v1, const std::string &v2);

private:
    ModelManager()=default;
    bool loadModelFile(const std::filesystem::path &filePath);
    bool parseModelInfo(const nlohmann::json &jsonData, ModelInfo &info) const;
    bool validateSchema(const nlohmann::json &config) const;

    std::vector<ModelInfo> m_models;
    std::map<std::string, std::string> m_modelProviderMap;
    bool m_initialized{ false };
};

} // namespace hermesaxiom

#endif//_hermesaxiom_modelManager_h_
