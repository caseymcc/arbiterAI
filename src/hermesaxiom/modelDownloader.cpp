#include "hermesaxiom/modelDownloader.h"
#include "hermesaxiom/modelManager.h"
#include <picosha2.h>
#include <cpr/cpr.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace hermesaxiom
{

ModelDownloader::ModelDownloader(std::shared_ptr<IFileVerifier> fileVerifier) : m_fileVerifier(fileVerifier)
{
    m_cacheDir=std::filesystem::temp_directory_path()/"hermesaxiom_cache";
    std::filesystem::create_directories(m_cacheDir);
    spdlog::debug("Initialized cache directory at: {}", m_cacheDir.string());
}

std::future<bool> ModelDownloader::downloadModel(const std::string &downloadUrl, const std::string &filePathStr, const std::optional<std::string> &fileHash, const std::optional<std::string> &minVersion, const std::optional<std::string> &maxVersion)
{
    return std::async(std::launch::async, [this, downloadUrl, filePathStr, fileHash, minVersion, maxVersion]()
        {
            // Check version compatibility first
            if(minVersion||maxVersion)
            {
                std::string clientVersion="1.0.0"; // TODO: Get from build config
                if((minVersion&&clientVersion<*minVersion)||
                    (maxVersion&&clientVersion>*maxVersion))
                {
                    spdlog::error("Version mismatch: client {} not compatible with model requirements (min: {}, max: {})",
                        clientVersion, minVersion?*minVersion:"none", maxVersion?*maxVersion:"none");
                    return false;
                }
            }

            std::filesystem::path filePath(filePathStr);
            if(std::filesystem::exists(filePath))
            {
                if(fileHash&&m_fileVerifier->verifyFile(filePath.string(), *fileHash))
                {
                    spdlog::info("Model already exists and is verified: {}", filePath.string());
                    return true;
                }
            }

            spdlog::info("Downloading model from {} to {}", downloadUrl, filePath.string());
            cpr::Response r=cpr::Get(cpr::Url{ downloadUrl });
            if(r.status_code!=200)
            {
                spdlog::error("Failed to download model. Status code: {}", r.status_code);
                return false;
            }

            try
            {
                std::filesystem::create_directories(filePath.parent_path());
                std::ofstream outFile(filePath, std::ios::binary);
                outFile.write(r.text.c_str(), r.text.length());
                outFile.close();
            }
            catch(const std::filesystem::filesystem_error &e)
            {
                spdlog::error("Failed to write model to file: {}. Error: {}", filePath.string(), e.what());
                return false;
            }

            if(fileHash)
            {
                if(m_fileVerifier->verifyFile(filePath.string(), *fileHash))
                {
                    spdlog::info("Model downloaded and verified successfully: {}", filePath.string());
                    return true;
                }
                else
                {
                    spdlog::error("SHA256 verification failed for: {}", filePath.string());
                    return false;
                }
            }

            return true;
        });
}


std::future<nlohmann::json> ModelDownloader::downloadConfigFromRepo(const std::string &repoOwner, const std::string &repoName, const std::string &configPath, const std::optional<std::string> &ref)
{
    return std::async(std::launch::async, [this, repoOwner, repoName, configPath, ref]()
    {
        std::string cacheKey=fmt::format("{}/{}/{}/{}", repoOwner, repoName, ref.value_or("main"), configPath);
        if(auto cached=loadFromCache(cacheKey))
        {
            spdlog::debug("Returning cached config for: {}", cacheKey);
            return *cached;
        }

        std::string apiUrl=fmt::format("https://api.github.com/repos/{}/{}/contents/{}", repoOwner, repoName, configPath);
        if(ref)
        {
            apiUrl+=fmt::format("?ref={}", *ref);
        }

        cpr::Response r=cpr::Get(cpr::Url{apiUrl},
            cpr::Header{{"Accept", "application/vnd.github.v3.raw"}});

        if(r.status_code!=200)
        {
            spdlog::error("GitHub API request failed for {}: {}", apiUrl, r.status_code);
            throw std::runtime_error(fmt::format("GitHub API request failed: {}", r.status_code));
        }

        auto config=parseConfigFromJSON(r.text);
        if(!config)
        {
            throw std::runtime_error("Failed to parse GitHub config");
        }

        saveToCache(cacheKey, *config);
        return *config;
    });
}

std::optional<nlohmann::json> ModelDownloader::parseConfigFromJSON(const std::string &jsonContent)
{
    try
    {
        return nlohmann::json::parse(jsonContent);
    }
    catch(const nlohmann::json::exception &e)
    {
        spdlog::error("JSON parsing failed: {}", e.what());
        return std::nullopt;
    }
}

std::string ModelDownloader::getCachePath(const std::string &key)
{
    std::string safeKey=key;
    std::replace(safeKey.begin(), safeKey.end(), '/', '_');
    return (m_cacheDir/safeKey).string();
}

std::optional<nlohmann::json> ModelDownloader::loadFromCache(const std::string &key)
{
    std::string cacheFile=getCachePath(key);
    if(!std::filesystem::exists(cacheFile))
    {
        return std::nullopt;
    }

    try
    {
        std::ifstream file(cacheFile);
        nlohmann::json cached;
        file>>cached;
        return cached;
    }
    catch(const std::exception &e)
    {
        spdlog::warn("Failed to load cached config {}: {}", key, e.what());
        return std::nullopt;
    }
}

void ModelDownloader::saveToCache(const std::string &key, const nlohmann::json &config)
{
    std::string cacheFile=getCachePath(key);
    try
    {
        std::ofstream file(cacheFile);
        file<<config.dump(4);
    }
    catch(const std::exception &e)
    {
        spdlog::warn("Failed to save config to cache {}: {}", key, e.what());
    }
}

} // namespace hermesaxiom