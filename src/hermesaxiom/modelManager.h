#ifndef _hermesaxiom_modelManager_h_
#define _hermesaxiom_modelManager_h_

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>

namespace hermesaxiom
{

struct ModelInfo
{
    std::string model;
    std::string provider;
    std::string mode{ "chat" };
    std::optional<std::string> apiBase;
    std::optional<std::string> apiKey;
    bool examplesAsSysMsg{ false };
    int contextWindow{ 4096 };
    int maxTokens{ 2048 };
    int maxInputTokens{ 3072 };
    int maxOutputTokens{ 1024 };
    double inputCostPerToken{ 0.0 };
    double outputCostPerToken{ 0.0 };
};

class ModelManager
{
public:
    static ModelManager &instance();

    bool initialize(const std::vector<std::filesystem::path> &configPaths);
    std::optional<std::string> getProvider(const std::string &model) const;
    std::optional<ModelInfo> getModelInfo(const std::string &model) const;
    void addModel(const ModelInfo& modelInfo);
    const std::map<std::string, std::string> &getModelProviderMap() const { return m_modelProviderMap; }

private:
    ModelManager()=default;
    bool loadModelFile(const std::filesystem::path &filePath);

    std::vector<ModelInfo> m_models;
    std::map<std::string, std::string> m_modelProviderMap;
    bool m_initialized{ false };
};

} // namespace hermesaxiom

#endif//_hermesaxiom_modelManager_h_
