#ifndef _ARBITER_AI_COST_MANAGER_H_
#define _ARBITER_AI_COST_MANAGER_H_

#include <filesystem>
#include <string>
#include <map>
#include <mutex>
#include <optional>
#include <functional>

namespace arbiterAI
{

/**
 * @struct SessionCost
 * @brief Cost tracking for a single session
 */
struct SessionCost
{
    double totalCost = 0.0;         ///< Total cost for this session
    int completionCount = 0;        ///< Number of completions
    std::optional<double> limit;    ///< Optional session-specific limit
};

/**
 * @brief Callback when spending limit is reached
 * @param sessionId The session that hit the limit (empty for global)
 * @param currentCost Current total cost
 * @param limit The limit that was exceeded
 */
using SpendingLimitCallback = std::function<void(const std::string& sessionId, 
                                                   double currentCost, 
                                                   double limit)>;

/**
 * @class CostManager
 * @brief Tracks and enforces spending limits for API calls
 *
 * Provides:
 * - Global spending limit enforcement
 * - Per-session cost tracking
 * - Session-level spending limits
 * - Persistent cost state
 */
class CostManager
{
public:
    /**
     * @brief Construct a CostManager
     * @param stateFile Path to persist cost state
     * @param spendingLimit Global spending limit (-1 for unlimited)
     */
    CostManager(std::filesystem::path stateFile, double spendingLimit);

    /**
     * @brief Check if a request can proceed within limits
     * @param estimatedCost Estimated cost of the request
     * @return true if within limits, false otherwise
     */
    bool canProceed(double estimatedCost);

    /**
     * @brief Check if a request can proceed for a specific session
     * @param sessionId Session identifier
     * @param estimatedCost Estimated cost of the request
     * @return true if within both session and global limits
     */
    bool canProceed(const std::string& sessionId, double estimatedCost);

    /**
     * @brief Record cost for a completed request
     * @param cost Actual cost incurred
     */
    void recordCost(double cost);

    /**
     * @brief Record cost for a specific session
     * @param sessionId Session identifier
     * @param cost Actual cost incurred
     */
    void recordCost(const std::string& sessionId, double cost);

    /**
     * @brief Get total global cost
     * @return Current total cost
     */
    double getTotalCost() const;

    /**
     * @brief Get cost for a specific session
     * @param sessionId Session identifier
     * @return Session cost information
     */
    SessionCost getSessionCost(const std::string& sessionId) const;

    /**
     * @brief Set a spending limit for a specific session
     * @param sessionId Session identifier
     * @param limit Spending limit for this session
     */
    void setSessionLimit(const std::string& sessionId, double limit);

    /**
     * @brief Remove a session's tracking
     * @param sessionId Session identifier
     */
    void removeSession(const std::string& sessionId);

    /**
     * @brief Set callback for when spending limit is reached
     * @param callback Function to call when limit exceeded
     */
    void setLimitReachedCallback(SpendingLimitCallback callback);

    /**
     * @brief Get remaining budget (global)
     * @return Remaining budget, or -1 if unlimited
     */
    double getRemainingBudget() const;

    /**
     * @brief Get remaining budget for a session
     * @param sessionId Session identifier
     * @return Remaining budget, or -1 if unlimited
     */
    double getSessionRemainingBudget(const std::string& sessionId) const;

private:
    void loadState();
    void saveState();

    std::filesystem::path m_stateFile;
    double m_spendingLimit;
    double m_currentCost;
    
    std::map<std::string, SessionCost> m_sessionCosts;
    SpendingLimitCallback m_limitCallback;
    
    mutable std::mutex m_mutex;
};

} // namespace arbiterAI

#endif // _ARBITER_AI_COST_MANAGER_H_