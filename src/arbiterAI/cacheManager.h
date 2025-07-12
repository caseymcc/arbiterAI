#ifndef _arbiterAI_cacheManager_h_
#define _arbiterAI_cacheManager_h_

#include <filesystem>
#include <chrono>
#include <optional>

#include "arbiterAI.h"

namespace arbiterAI
{

class CacheManager
{
public:
    CacheManager(std::filesystem::path cacheDir, std::chrono::seconds ttl);

    std::optional<CompletionResponse> get(const CompletionRequest& request);
    void put(const CompletionRequest& request, const CompletionResponse& response);

private:
    std::filesystem::path m_cacheDir;
    std::chrono::seconds m_ttl;

    std::string generateKey(const CompletionRequest& request) const;
};

} // namespace arbiterAI

#endif // _arbiterAI_cacheManager_h_