#include "cacheManager.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <nlohmann/json.hpp>

namespace arbiterAI
{

CacheManager::CacheManager(std::filesystem::path cacheDir, 
                           std::chrono::seconds ttl,
                           const std::string& sessionId)
    : m_cacheDir(std::move(cacheDir))
    , m_ttl(ttl)
    , m_sessionId(sessionId)
    , m_stats{}
{
    // Append session ID to cache directory if provided
    if (!m_sessionId.empty())
    {
        m_cacheDir = m_cacheDir / m_sessionId;
    }

    if (!std::filesystem::exists(m_cacheDir))
    {
        std::filesystem::create_directories(m_cacheDir);
    }
}

std::optional<CompletionResponse> CacheManager::get(const CompletionRequest &request)
{
    const auto key = generateKey(request);
    const auto path = m_cacheDir / key;

    if (!std::filesystem::exists(path))
    {
        m_stats.misses++;
        return std::nullopt;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        m_stats.misses++;
        return std::nullopt;
    }

    nlohmann::json j;
    file >> j;

    const auto timestamp = std::chrono::system_clock::from_time_t(j.at("timestamp").get<std::time_t>());
    const auto now = std::chrono::system_clock::now();

    if (now - timestamp > m_ttl)
    {
        std::filesystem::remove(path);
        m_stats.evictions++;
        m_stats.misses++;
        return std::nullopt;
    }

    m_stats.hits++;
    return j.at("response").get<CompletionResponse>();
}

void CacheManager::put(const CompletionRequest &request, const CompletionResponse &response)
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

    m_stats.puts++;
}

CacheStats CacheManager::getStats() const
{
    return m_stats;
}

void CacheManager::resetStats()
{
    m_stats = CacheStats{};
}

void CacheManager::clear()
{
    if (std::filesystem::exists(m_cacheDir))
    {
        for (const auto& entry : std::filesystem::directory_iterator(m_cacheDir))
        {
            std::filesystem::remove(entry.path());
        }
    }
    m_stats.evictions += m_stats.puts;  // All entries evicted
}

double CacheManager::getHitRate() const
{
    int total = m_stats.hits + m_stats.misses;
    if (total == 0)
    {
        return 0.0;
    }
    return (static_cast<double>(m_stats.hits) / total) * 100.0;
}

std::string CacheManager::generateKey(const CompletionRequest &request) const
{
    nlohmann::json j = request;
    
    // Include session ID in key if present (for session-scoped caching)
    if (!m_sessionId.empty())
    {
        j["_session_id"] = m_sessionId;
    }
    
    const auto serialized = j.dump();

    unsigned char hash[SHA256_DIGEST_LENGTH];
    EVP_MD_CTX *sha256 = EVP_MD_CTX_new();
    
    if (!sha256)
        return "";

    if (EVP_DigestInit_ex(sha256, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(sha256, serialized.c_str(), serialized.size()) != 1 ||
        EVP_DigestFinal_ex(sha256, hash, nullptr) != 1)
    {
        EVP_MD_CTX_free(sha256);
        return "";
    }
    EVP_MD_CTX_free(sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

} // namespace arbiterAI