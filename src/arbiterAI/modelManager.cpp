#include "arbiterAI/modelManager.h"
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>
#include <fstream>
#include <iostream>
#include <cpr/cpr.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>

namespace arbiterAI
{

bool ModelInfo::isSchemaCompatible(const std::string &schemaVersion) const
{
    if(minSchemaVersion.empty()||schemaVersion.empty())
    {
        return true;
    }
    return ModelManager::compareVersions(schemaVersion, minSchemaVersion)>=0;
}

ModelManager &ModelManager::instance()
{
    static ModelManager instance;
    return instance;
}

void ModelManager::reset()
{
    instance()=ModelManager();
}

bool ModelManager::initialize(const std::vector<std::filesystem::path> &configPaths, const std::filesystem::path &localOverridePath)
{
    // Clear existing data
    m_models.clear();
    m_modelProviderMap.clear();

    bool anyLoaded=false;

    // Initialize ConfigDownloader
    const std::string remoteUrl="https://github.com/caseymcc/arbiterAI_config.git";
    std::filesystem::path localPath=std::filesystem::temp_directory_path()/"arbiterAI_config";

    m_configDownloader.initialize(remoteUrl, localPath);
    // Load configs from the downloaded repository
    auto remoteModelsPath=m_configDownloader.getLocalPath()/"models";
    if(std::filesystem::exists(remoteModelsPath))
    {
        for(const auto &entry:std::filesystem::directory_iterator(remoteModelsPath))
        {
            if(entry.path().extension()==".json")
            {
                if(loadModelFile(entry.path()))
                {
                    anyLoaded=true;
                }
            }
        }
    }

    // Process additional local directories from configPaths
    for(const auto &configPath:configPaths)
    {
        auto modelsPath=configPath/"models";
        if(std::filesystem::exists(modelsPath))
        {
            for(const auto &entry:std::filesystem::directory_iterator(modelsPath))
            {
                if(entry.path().extension()==".json")
                {
                    if(loadModelFile(entry.path()))
                    {
                        anyLoaded=true;
                    }
                }
            }
        }
    }

    // Process local override directory
    if(!localOverridePath.empty()&&std::filesystem::exists(localOverridePath))
    {
        for(const auto &entry:std::filesystem::directory_iterator(localOverridePath))
        {
            if(entry.path().extension()==".json")
            {
                if(loadModelFile(entry.path()))
                {
                    anyLoaded=true;
                }
            }
        }
    }

    m_initialized=anyLoaded;
    return anyLoaded;
}

bool ModelManager::validateSchema(const nlohmann::json &config) const
{
    static nlohmann::json schema;
    static bool schemaLoaded=false;

    // Load schema once
    if(!schemaLoaded)
    {
        try
        {
            std::ifstream schemaFile("schemas/model_config.schema.json");
            if(!schemaFile.is_open())
            {
                spdlog::error("Failed to open schema file");
                return false;
            }
            schema=nlohmann::json::parse(schemaFile);
            schemaLoaded=true;
        }
        catch(const std::exception &e)
        {
            spdlog::error("Failed to load schema: {}", e.what());
            return false;
        }
    }

    // Validate against schema
    try
    {
        nlohmann::json_schema::json_validator validator;
        validator.set_root_schema(schema);
        validator.validate(config);
        return true;
    }
    catch(const std::exception &e)
    {
        spdlog::warn("Schema validation failed: {}", e.what());
        return false;
    }
}

bool ModelManager::parseModelInfo(const nlohmann::json &modelJson, ModelInfo &info) const
{
    // Required fields
    if(!modelJson.contains("model")||!modelJson.contains("provider"))
    {
        return false;
    }

    info.model=modelJson["model"].get<std::string>();
    info.provider=modelJson["provider"].get<std::string>();

    // Schema version
    if(modelJson.contains("version"))
    {
        info.configVersion=modelJson["version"].get<std::string>();
    }

    // Model ranking
    if(modelJson.contains("ranking"))
    {
        info.ranking=modelJson["ranking"].get<int>();
    }

    // Download metadata
    if(modelJson.contains("download"))
    {
        DownloadMetadata dl;
        auto &download=modelJson["download"];
        if(download.contains("url"))
        {
            dl.url=download["url"].get<std::string>();
        }
        if(download.contains("sha256"))
        {
            dl.sha256=download["sha256"].get<std::string>();
        }
        if(download.contains("cachePath"))
        {
            dl.cachePath=download["cachePath"].get<std::string>();
        }
        info.download=dl;
    }

    // Version compatibility
    if(modelJson.contains("compatibility"))
    {
        auto &compat=modelJson["compatibility"];
        if(compat.contains("min_client_version"))
        {
            info.minClientVersion=compat["min_client_version"].get<std::string>();
        }
        if(compat.contains("max_client_version"))
        {
            info.maxClientVersion=compat["max_client_version"].get<std::string>();
        }
    }

    return true;
}

int ModelManager::compareVersions(const std::string &v1, const std::string &v2)
{
    std::vector<int> v1Parts, v2Parts;
    std::stringstream ss1(v1), ss2(v2);
    std::string part;

    while(std::getline(ss1, part, '.')) v1Parts.push_back(std::stoi(part));
    while(std::getline(ss2, part, '.')) v2Parts.push_back(std::stoi(part));

    for(size_t i=0; i<std::min(v1Parts.size(), v2Parts.size()); ++i)
    {
        if(v1Parts[i]<v2Parts[i]) return -1;
        if(v1Parts[i]>v2Parts[i]) return 1;
    }

    if(v1Parts.size()<v2Parts.size()) return -1;
    if(v1Parts.size()>v2Parts.size()) return 1;
    return 0;
}

std::vector<ModelInfo> ModelManager::getModelsByRanking() const
{
    auto models=m_models;
    std::sort(models.begin(), models.end(),
        [](const ModelInfo &a, const ModelInfo &b)
        {
            // First sort by ranking (higher first)
            if(a.ranking!=b.ranking)
            {
                return a.ranking>b.ranking;
            }
            // Then by name (case-insensitive alphabetical)
            return std::lexicographical_compare(
                a.model.begin(), a.model.end(),
                b.model.begin(), b.model.end(),
                [](char c1, char c2)
                {
                    return std::tolower(c1)<std::tolower(c2);
                });
        });
    return models;
}

bool ModelManager::loadModelFile(const std::filesystem::path &filePath)
{
    try
    {
        std::ifstream file(filePath);
        nlohmann::json config=nlohmann::json::parse(file);

        if(!config.contains("models")||!config["models"].is_array())
        {
            return false;
        }

        for(const auto &modelJson:config["models"])
        {
            ModelInfo info;
            if(!parseModelInfo(modelJson, info))
            {
                continue;
            }

            // Optional fields
            if(modelJson.contains("mode"))
            {
                info.mode=modelJson["mode"].get<std::string>();
            }
            if(modelJson.contains("api_base"))
            {
                info.apiBase=modelJson["api_base"].get<std::string>();
            }
            if(modelJson.contains("file_path"))
            {
                info.filePath=modelJson["file_path"].get<std::string>();
            }
            if(modelJson.contains("api_key"))
            {
                info.apiKey=modelJson["api_key"].get<std::string>();
            }
            if(modelJson.contains("examples_as_sys_msg"))
            {
                info.examplesAsSysMsg=modelJson["examples_as_sys_msg"].get<bool>();
            }
            if(modelJson.contains("context_window"))
            {
                info.contextWindow=modelJson["context_window"].get<int>();
            }
            if(modelJson.contains("max_tokens"))
            {
                info.maxTokens=modelJson["max_tokens"].get<int>();
            }
            if(modelJson.contains("max_input_tokens"))
            {
                info.maxInputTokens=modelJson["max_input_tokens"].get<int>();
            }
            if(modelJson.contains("max_output_tokens"))
            {
                info.maxOutputTokens=modelJson["max_output_tokens"].get<int>();
            }
            if(modelJson.contains("pricing"))
            {
                info.pricing=modelJson["pricing"].get<Pricing>();
            }

            // Find existing model to update
            auto it=std::find_if(m_models.begin(), m_models.end(),
                [&info](const ModelInfo &existing) { return existing.model==info.model; });

            if(it!=m_models.end())
            {
                // Update existing model settings
                if(modelJson.contains("mode"))
                {
                    it->mode=info.mode;
                }
                if(modelJson.contains("api_base"))
                {
                    it->apiBase=info.apiBase;
                }
                if(modelJson.contains("file_path"))
                {
                    it->filePath=info.filePath;
                }
                if(modelJson.contains("api_key"))
                {
                    it->apiKey=info.apiKey;
                }
                if(modelJson.contains("examples_as_sys_msg"))
                {
                    it->examplesAsSysMsg=info.examplesAsSysMsg;
                }
                if(modelJson.contains("context_window"))
                {
                    it->contextWindow=info.contextWindow;
                }
                if(modelJson.contains("max_tokens"))
                {
                    it->maxTokens=info.maxTokens;
                }
                if(modelJson.contains("max_input_tokens"))
                {
                    it->maxInputTokens=info.maxInputTokens;
                }
                if(modelJson.contains("max_output_tokens"))
                {
                    it->maxOutputTokens=info.maxOutputTokens;
                }
                if(modelJson.contains("pricing"))
                {
                    it->pricing=info.pricing;
                }
            }
            else
            {
                // Add new model
                m_models.push_back(info);
            }
            m_modelProviderMap[info.model]=info.provider;
        }

        return true;
    }
    catch(const std::exception &)
    {
        return false;
    }
}

std::optional<std::string> ModelManager::getProvider(const std::string &model) const
{
    auto it=m_modelProviderMap.find(model);
    if(it!=m_modelProviderMap.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::optional<ModelInfo> ModelManager::getModelInfo(const std::string &model) const
{
    auto it=std::find_if(m_models.begin(), m_models.end(),
        [&model](const ModelInfo &info) { return info.model==model; });

    if(it!=m_models.end())
    {
        return *it;
    }
    return std::nullopt;
}

std::vector<ModelInfo> ModelManager::getModels(const std::string &provider) const
{
    std::vector<ModelInfo> result;
    for(const auto &model:m_models)
    {
        if(model.provider==provider)
        {
            result.push_back(model);
        }
    }
    return result;
}

void ModelManager::addModel(const ModelInfo &modelInfo)
{
    auto it=std::find_if(m_models.begin(), m_models.end(),
        [&](const ModelInfo &existing) { return existing.model==modelInfo.model; });

    if(it==m_models.end())
    {
        m_models.push_back(modelInfo);
        m_modelProviderMap[modelInfo.model]=modelInfo.provider;
    }
}

} // namespace arbiterAI
