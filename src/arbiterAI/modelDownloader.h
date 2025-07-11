#ifndef _arbiterAI_modelDownloader_h_
#define _arbiterAI_modelDownloader_h_

#include "arbiterAI/modelManager.h"
#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/fileVerifier.h"

#include <string>
#include <future>
#include <optional>
#include <nlohmann/json.hpp>
#include <memory>

namespace arbiterAI
{

class ModelDownloader
{
public:
    ModelDownloader(std::shared_ptr<IFileVerifier> fileVerifier = std::make_shared<FileVerifier>());

    std::future<bool> downloadModel(const std::string &downloadUrl, const std::string &filePath, const std::optional<std::string> &fileHash, const std::optional<std::string> &minClientVersion=std::nullopt, const std::optional<std::string> &maxClientVersion=std::nullopt);

    // GitHub API functions
    std::future<nlohmann::json> downloadConfigFromRepo(const std::string &repoOwner, const std::string &repoName, const std::string &configPath, const std::optional<std::string> &ref=std::nullopt);
    std::optional<nlohmann::json> parseConfigFromJSON(const std::string &jsonContent);

private:
    std::string getCachePath(const std::string &key);
    std::optional<nlohmann::json> loadFromCache(const std::string &key);
    void saveToCache(const std::string &key, const nlohmann::json &config);

    std::filesystem::path m_cacheDir;
    std::shared_ptr<IFileVerifier> m_fileVerifier;
};

} // namespace arbiterAI

#endif // _arbiterAI_modelDownloader_h_