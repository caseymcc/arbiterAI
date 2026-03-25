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

bool ModelInfo::isCompatible(const std::string &clientVersion) const
{
    if (minClientVersion.has_value() && ModelManager::compareVersions(clientVersion, minClientVersion.value()) < 0)
    {
        return false;
    }
    if (maxClientVersion.has_value() && ModelManager::compareVersions(clientVersion, maxClientVersion.value()) > 0)
    {
        return false;
    }
    return true;
}

bool ModelInfo::isSchemaCompatible(const std::string &schemaVersion) const
{
    if (minSchemaVersion.empty() || schemaVersion.empty())
    {
        return true;
    }
    // Check if schemaVersion is >= minSchemaVersion and <= configVersion
    return ModelManager::compareVersions(schemaVersion, minSchemaVersion) >= 0 &&
           ModelManager::compareVersions(schemaVersion, configVersion) <= 0;
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
    std::filesystem::path remotePath=std::filesystem::current_path()/"arbiterAI_config";

    ConfigDownloadStatus dlStatus=m_configDownloader.initialize(remoteUrl, remotePath);
    if(dlStatus==ConfigDownloadStatus::Success||dlStatus==ConfigDownloadStatus::FallbackToCache)
    {
        // Load configs from the downloaded repository
        // The config repo has models in configs/defaults/models/
        auto remoteModelsPath=m_configDownloader.getLocalPath()/"configs"/"defaults"/"models";
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
    }
    else
    {
        spdlog::warn("Config download failed (status {}), skipping remote configs", static_cast<int>(dlStatus));
    }

    // Process additional local directories from configPaths
    for(const auto &configPath:configPaths)
    {
        auto modelsPath=configPath/"models";
        spdlog::info("Looking for models in: {}", modelsPath.string());
        if(std::filesystem::exists(modelsPath))
        {
            spdlog::info("Found models directory: {}", modelsPath.string());
            for(const auto &entry:std::filesystem::directory_iterator(modelsPath))
            {
                if(entry.path().extension()==".json")
                {
                    spdlog::info("Loading model file: {}", entry.path().string());
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

    // Hardware requirements (local models)
    if(modelJson.contains("hardware_requirements"))
    {
        auto &hw=modelJson["hardware_requirements"];

        HardwareRequirements reqs;
        if(hw.contains("min_system_ram_mb"))
        {
            reqs.minSystemRamMb=hw["min_system_ram_mb"].get<int>();
        }
        if(hw.contains("parameter_count"))
        {
            reqs.parameterCount=hw["parameter_count"].get<std::string>();
        }
        info.hardwareRequirements=reqs;
    }

    // Context scaling (local models)
    if(modelJson.contains("context_scaling"))
    {
        auto &cs=modelJson["context_scaling"];

        ContextScaling scaling;
        if(cs.contains("base_context"))
        {
            scaling.baseContext=cs["base_context"].get<int>();
        }
        if(cs.contains("max_context"))
        {
            scaling.maxContext=cs["max_context"].get<int>();
        }
        if(cs.contains("vram_per_1k_context_mb"))
        {
            scaling.vramPer1kContextMb=cs["vram_per_1k_context_mb"].get<int>();
        }
        info.contextScaling=scaling;
    }

    // Variants (quantization variants for local models)
    if(modelJson.contains("variants")&&modelJson["variants"].is_array())
    {
        for(const auto &variantJson:modelJson["variants"])
        {
            ModelVariant variant;

            if(variantJson.contains("quantization"))
            {
                variant.quantization=variantJson["quantization"].get<std::string>();
            }
            if(variantJson.contains("file_size_mb"))
            {
                variant.fileSizeMb=variantJson["file_size_mb"].get<int>();
            }
            if(variantJson.contains("min_vram_mb"))
            {
                variant.minVramMb=variantJson["min_vram_mb"].get<int>();
            }
            if(variantJson.contains("recommended_vram_mb"))
            {
                variant.recommendedVramMb=variantJson["recommended_vram_mb"].get<int>();
            }
            if(variantJson.contains("download"))
            {
                auto &dl=variantJson["download"];
                if(dl.contains("url"))
                {
                    variant.download.url=dl["url"].get<std::string>();
                }
                if(dl.contains("sha256"))
                {
                    variant.download.sha256=dl["sha256"].get<std::string>();
                }
                if(dl.contains("filename"))
                {
                    variant.download.filename=dl["filename"].get<std::string>();
                }
            }
            info.variants.push_back(variant);
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
                mergeModelInfo(*it, info, modelJson);
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
    }
    m_modelProviderMap[modelInfo.model]=modelInfo.provider;
}

bool ModelManager::validateModelJson(const nlohmann::json &modelJson, std::string &error) const
{
    // Required fields check (independent of schema file)
    if(!modelJson.contains("model")||!modelJson.contains("provider"))
    {
        error="Missing required field: 'model' and 'provider' are required";
        return false;
    }

    // Schema validation (best-effort — skipped if schema file not available)
    nlohmann::json envelope={
        {"schema_version", "1.1.0"},
        {"models", nlohmann::json::array({modelJson})}
    };

    validateSchema(envelope);
    return true;
}

void ModelManager::mergeModelInfo(ModelInfo &existing, const ModelInfo &source, const nlohmann::json &sourceJson) const
{
    existing.provider=source.provider;
    existing.ranking=source.ranking;

    if(sourceJson.contains("mode"))
        existing.mode=source.mode;
    if(sourceJson.contains("api_base"))
        existing.apiBase=source.apiBase;
    if(sourceJson.contains("file_path"))
        existing.filePath=source.filePath;
    if(sourceJson.contains("api_key"))
        existing.apiKey=source.apiKey;
    if(sourceJson.contains("examples_as_sys_msg"))
        existing.examplesAsSysMsg=source.examplesAsSysMsg;
    if(sourceJson.contains("context_window"))
        existing.contextWindow=source.contextWindow;
    if(sourceJson.contains("max_tokens"))
        existing.maxTokens=source.maxTokens;
    if(sourceJson.contains("max_input_tokens"))
        existing.maxInputTokens=source.maxInputTokens;
    if(sourceJson.contains("max_output_tokens"))
        existing.maxOutputTokens=source.maxOutputTokens;
    if(sourceJson.contains("pricing"))
        existing.pricing=source.pricing;
    if(sourceJson.contains("hardware_requirements"))
        existing.hardwareRequirements=source.hardwareRequirements;
    if(sourceJson.contains("context_scaling"))
        existing.contextScaling=source.contextScaling;
    if(sourceJson.contains("variants"))
        existing.variants=source.variants;
    if(sourceJson.contains("download"))
        existing.download=source.download;
    if(sourceJson.contains("version"))
        existing.configVersion=source.configVersion;
}

bool ModelManager::addModelFromJson(const nlohmann::json &modelJson, std::string &error)
{
    if(!validateModelJson(modelJson, error))
        return false;

    ModelInfo info;
    if(!parseModelInfo(modelJson, info))
    {
        error="Failed to parse model info";
        return false;
    }

    // Parse optional fields that parseModelInfo doesn't cover
    if(modelJson.contains("mode"))
        info.mode=modelJson["mode"].get<std::string>();
    if(modelJson.contains("api_base"))
        info.apiBase=modelJson["api_base"].get<std::string>();
    if(modelJson.contains("file_path"))
        info.filePath=modelJson["file_path"].get<std::string>();
    if(modelJson.contains("api_key"))
        info.apiKey=modelJson["api_key"].get<std::string>();
    if(modelJson.contains("examples_as_sys_msg"))
        info.examplesAsSysMsg=modelJson["examples_as_sys_msg"].get<bool>();
    if(modelJson.contains("context_window"))
        info.contextWindow=modelJson["context_window"].get<int>();
    if(modelJson.contains("max_tokens"))
        info.maxTokens=modelJson["max_tokens"].get<int>();
    if(modelJson.contains("max_input_tokens"))
        info.maxInputTokens=modelJson["max_input_tokens"].get<int>();
    if(modelJson.contains("max_output_tokens"))
        info.maxOutputTokens=modelJson["max_output_tokens"].get<int>();
    if(modelJson.contains("pricing"))
        info.pricing=modelJson["pricing"].get<Pricing>();

    // Check for duplicate
    auto it=std::find_if(m_models.begin(), m_models.end(),
        [&info](const ModelInfo &existing) { return existing.model==info.model; });

    if(it!=m_models.end())
    {
        error="Model already exists: "+info.model;
        return false;
    }

    m_models.push_back(info);
    m_modelProviderMap[info.model]=info.provider;
    m_runtimeModels.insert(info.model);
    return true;
}

bool ModelManager::updateModelFromJson(const nlohmann::json &modelJson, std::string &error)
{
    if(!validateModelJson(modelJson, error))
        return false;

    ModelInfo info;
    if(!parseModelInfo(modelJson, info))
    {
        error="Failed to parse model info";
        return false;
    }

    // Parse optional fields that parseModelInfo doesn't cover
    if(modelJson.contains("mode"))
        info.mode=modelJson["mode"].get<std::string>();
    if(modelJson.contains("api_base"))
        info.apiBase=modelJson["api_base"].get<std::string>();
    if(modelJson.contains("file_path"))
        info.filePath=modelJson["file_path"].get<std::string>();
    if(modelJson.contains("api_key"))
        info.apiKey=modelJson["api_key"].get<std::string>();
    if(modelJson.contains("examples_as_sys_msg"))
        info.examplesAsSysMsg=modelJson["examples_as_sys_msg"].get<bool>();
    if(modelJson.contains("context_window"))
        info.contextWindow=modelJson["context_window"].get<int>();
    if(modelJson.contains("max_tokens"))
        info.maxTokens=modelJson["max_tokens"].get<int>();
    if(modelJson.contains("max_input_tokens"))
        info.maxInputTokens=modelJson["max_input_tokens"].get<int>();
    if(modelJson.contains("max_output_tokens"))
        info.maxOutputTokens=modelJson["max_output_tokens"].get<int>();
    if(modelJson.contains("pricing"))
        info.pricing=modelJson["pricing"].get<Pricing>();

    auto it=std::find_if(m_models.begin(), m_models.end(),
        [&info](const ModelInfo &existing) { return existing.model==info.model; });

    if(it!=m_models.end())
    {
        mergeModelInfo(*it, info, modelJson);
    }
    else
    {
        m_models.push_back(info);
    }

    m_modelProviderMap[info.model]=info.provider;
    m_runtimeModels.insert(info.model);
    return true;
}

bool ModelManager::removeModel(const std::string &modelName)
{
    auto it=std::find_if(m_models.begin(), m_models.end(),
        [&modelName](const ModelInfo &info) { return info.model==modelName; });

    if(it==m_models.end())
        return false;

    m_models.erase(it);
    m_modelProviderMap.erase(modelName);
    m_runtimeModels.erase(modelName);
    return true;
}

nlohmann::json ModelManager::modelInfoToJson(const ModelInfo &info)
{
    nlohmann::json j;
    j["model"]=info.model;
    j["provider"]=info.provider;
    j["mode"]=info.mode;
    j["ranking"]=info.ranking;
    j["context_window"]=info.contextWindow;
    j["max_tokens"]=info.maxTokens;
    j["max_input_tokens"]=info.maxInputTokens;
    j["max_output_tokens"]=info.maxOutputTokens;

    if(info.apiBase.has_value())
        j["api_base"]=info.apiBase.value();
    if(info.filePath.has_value())
        j["file_path"]=info.filePath.value();
    if(info.apiKey.has_value())
        j["api_key"]=info.apiKey.value();

    if(info.pricing.prompt_token_cost>0.0||info.pricing.completion_token_cost>0.0)
    {
        j["pricing"]={
            {"prompt_token_cost", info.pricing.prompt_token_cost},
            {"completion_token_cost", info.pricing.completion_token_cost}
        };
    }

    if(info.download.has_value())
    {
        nlohmann::json dl;
        dl["url"]=info.download->url;
        dl["sha256"]=info.download->sha256;
        if(!info.download->cachePath.empty())
            dl["cachePath"]=info.download->cachePath;
        j["download"]=dl;
    }

    if(info.minClientVersion.has_value()||info.maxClientVersion.has_value())
    {
        nlohmann::json compat;
        if(info.minClientVersion.has_value())
            compat["min_client_version"]=info.minClientVersion.value();
        if(info.maxClientVersion.has_value())
            compat["max_client_version"]=info.maxClientVersion.value();
        j["compatibility"]=compat;
    }

    if(info.hardwareRequirements.has_value())
    {
        nlohmann::json hw;
        hw["min_system_ram_mb"]=info.hardwareRequirements->minSystemRamMb;
        if(!info.hardwareRequirements->parameterCount.empty())
            hw["parameter_count"]=info.hardwareRequirements->parameterCount;
        j["hardware_requirements"]=hw;
    }

    if(info.contextScaling.has_value())
    {
        j["context_scaling"]={
            {"base_context", info.contextScaling->baseContext},
            {"max_context", info.contextScaling->maxContext},
            {"vram_per_1k_context_mb", info.contextScaling->vramPer1kContextMb}
        };
    }

    if(!info.variants.empty())
    {
        nlohmann::json variants=nlohmann::json::array();
        for(const ModelVariant &v:info.variants)
        {
            nlohmann::json vj;
            vj["quantization"]=v.quantization;
            vj["file_size_mb"]=v.fileSizeMb;
            vj["min_vram_mb"]=v.minVramMb;
            vj["recommended_vram_mb"]=v.recommendedVramMb;

            if(!v.download.url.empty())
            {
                vj["download"]={
                    {"url", v.download.url},
                    {"sha256", v.download.sha256},
                    {"filename", v.download.filename}
                };
            }
            variants.push_back(vj);
        }
        j["variants"]=variants;
    }

    return j;
}

bool ModelManager::saveOverrides(const std::filesystem::path &overridePath) const
{
    if(m_runtimeModels.empty())
        return true;

    nlohmann::json models=nlohmann::json::array();
    for(const ModelInfo &info:m_models)
    {
        if(m_runtimeModels.count(info.model))
        {
            models.push_back(modelInfoToJson(info));
        }
    }

    nlohmann::json config={
        {"schema_version", "1.1.0"},
        {"models", models}
    };

    // Atomic write: write to temp file, then rename
    std::filesystem::path tempPath=overridePath.string()+".tmp";

    std::ofstream file(tempPath);
    if(!file.is_open())
    {
        spdlog::error("Failed to open override file for writing: {}", tempPath.string());
        return false;
    }

    file<<config.dump(4);
    file.close();

    std::error_code ec;
    std::filesystem::rename(tempPath, overridePath, ec);
    if(ec)
    {
        spdlog::error("Failed to rename override file: {}", ec.message());
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    return true;
}

} // namespace arbiterAI
