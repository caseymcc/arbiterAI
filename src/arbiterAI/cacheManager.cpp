#include "cacheManager.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include <openssl/sha.h>
#include <nlohmann/json.hpp>

namespace arbiterAI
{

CacheManager::CacheManager(std::filesystem::path cacheDir, std::chrono::seconds ttl)
    : m_cacheDir(std::move(cacheDir))
    , m_ttl(ttl)
{
    if (!std::filesystem::exists(m_cacheDir))
    {
        std::filesystem::create_directories(m_cacheDir);
    }
}

std::optional<CompletionResponse> CacheManager::get(const CompletionRequest& request)
{
    const auto key = generateKey(request);
    const auto path = m_cacheDir / key;

    if (!std::filesystem::exists(path))
    {
        return std::nullopt;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    nlohmann::json j;
    file >> j;

    const auto timestamp = std::chrono::system_clock::from_time_t(j.at("timestamp").get<std::time_t>());
    const auto now = std::chrono::system_clock::now();

    if (now - timestamp > m_ttl)
    {
        std::filesystem::remove(path);
        return std::nullopt;
    }

    return j.at("response").get<CompletionResponse>();
}

void CacheManager::put(const CompletionRequest& request, const CompletionResponse& response)
{
    const auto key = generateKey(request);
    const auto path = m_cacheDir / key;

    const auto now = std::chrono::system_clock::now();
    const auto timestamp = std::chrono::system_clock::to_time_t(now);

    nlohmann::json j = {
        {"response", response},
        {"timestamp", timestamp}
    };

    std::ofstream file(path);
    file << j.dump(4);
}

std::string CacheManager::generateKey(const CompletionRequest& request) const
{
    nlohmann::json j = request;
    const auto serialized = j.dump();

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, serialized.c_str(), serialized.size());
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

} // namespace arbiterAI