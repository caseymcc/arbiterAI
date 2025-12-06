#ifndef _arbiterAI_cacheManager_h_
#define _arbiterAI_cacheManager_h_

#include <filesystem>
#include <chrono>
#include <optional>
#include <atomic>
#include <string>

#include "arbiterAI.h"

namespace arbiterAI
{

/**
 * @struct CacheStats
 * @brief Statistics for cache operations
 */
struct CacheStats
{
    int hits = 0;           ///< Number of cache hits
    int misses = 0;         ///< Number of cache misses
    int puts = 0;           ///< Number of cache puts
    int evictions = 0;      ///< Number of cache evictions (TTL expired)
};

/**
 * @class CacheManager
 * @brief File-based cache for completion responses
 *
 * Provides caching with:
 * - TTL-based expiration
 * - Session-scoped or global caching
 * - Statistics tracking
 */
class CacheManager
{
public:
    /**
     * @brief Construct a CacheManager
     * @param cacheDir Directory for cache files
     * @param ttl Time-to-live for cache entries
     * @param sessionId Optional session ID for session-scoped caching
     */
    CacheManager(std::filesystem::path cacheDir, 
                 std::chrono::seconds ttl,
                 const std::string& sessionId = "");

    /**
     * @brief Get a cached response
     * @param request The completion request to look up
     * @return Cached response if found and valid, nullopt otherwise
     */
    std::optional<CompletionResponse> get(const CompletionRequest& request);

    /**
     * @brief Store a response in the cache
     * @param request The completion request (used as key)
     * @param response The response to cache
     */
    void put(const CompletionRequest& request, const CompletionResponse& response);

    /**
     * @brief Get cache statistics
     * @return Current cache statistics
     */
    CacheStats getStats() const;

    /**
     * @brief Reset cache statistics
     */
    void resetStats();

    /**
     * @brief Clear all cached entries
     */
    void clear();

    /**
     * @brief Get the cache hit rate
     * @return Hit rate as a percentage (0-100)
     */
    double getHitRate() const;

private:
    std::string generateKey(const CompletionRequest& request) const;

    std::filesystem::path m_cacheDir;
    std::chrono::seconds m_ttl;
    std::string m_sessionId;
    
    // Statistics (mutable for const getters)
    mutable CacheStats m_stats;
};

} // namespace arbiterAI

#endif // _arbiterAI_cacheManager_h_