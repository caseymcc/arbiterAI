#include "arbiterAI/costManager.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace arbiterAI
{

CostManager::CostManager(std::filesystem::path stateFile, double spendingLimit)
    : m_stateFile(std::move(stateFile))
    , m_spendingLimit(spendingLimit)
    , m_currentCost(0.0)
{
    loadState();
}

bool CostManager::canProceed(double estimatedCost)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_spendingLimit < 0)
    {
        return true;  // Unlimited
    }
    
    bool canProceed = (m_currentCost + estimatedCost <= m_spendingLimit);
    
    if (!canProceed && m_limitCallback)
    {
        m_limitCallback("", m_currentCost, m_spendingLimit);
    }
    
    return canProceed;
}

bool CostManager::canProceed(const std::string& sessionId, double estimatedCost)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Check global limit first
    if (m_spendingLimit >= 0 && m_currentCost + estimatedCost > m_spendingLimit)
    {
        if (m_limitCallback)
        {
            m_limitCallback("", m_currentCost, m_spendingLimit);
        }
        return false;
    }
    
    // Check session limit
    auto it = m_sessionCosts.find(sessionId);
    if (it != m_sessionCosts.end() && it->second.limit.has_value())
    {
        if (it->second.totalCost + estimatedCost > it->second.limit.value())
        {
            if (m_limitCallback)
            {
                m_limitCallback(sessionId, it->second.totalCost, it->second.limit.value());
            }
            return false;
        }
    }
    
    return true;
}

void CostManager::recordCost(double cost)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentCost += cost;
    saveState();
}

void CostManager::recordCost(const std::string& sessionId, double cost)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Update global cost
    m_currentCost += cost;
    
    // Update session cost
    auto& session = m_sessionCosts[sessionId];
    session.totalCost += cost;
    session.completionCount++;
    
    saveState();
}

double CostManager::getTotalCost() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentCost;
}

SessionCost CostManager::getSessionCost(const std::string& sessionId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_sessionCosts.find(sessionId);
    if (it != m_sessionCosts.end())
    {
        return it->second;
    }
    return SessionCost{};
}

void CostManager::setSessionLimit(const std::string& sessionId, double limit)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionCosts[sessionId].limit = limit;
}

void CostManager::removeSession(const std::string& sessionId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionCosts.erase(sessionId);
}

void CostManager::setLimitReachedCallback(SpendingLimitCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_limitCallback = std::move(callback);
}

double CostManager::getRemainingBudget() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_spendingLimit < 0)
    {
        return -1;  // Unlimited
    }
    
    return m_spendingLimit - m_currentCost;
}

double CostManager::getSessionRemainingBudget(const std::string& sessionId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_sessionCosts.find(sessionId);
    if (it != m_sessionCosts.end() && it->second.limit.has_value())
    {
        return it->second.limit.value() - it->second.totalCost;
    }
    
    return -1;  // Unlimited
}

void CostManager::loadState()
{
    if (std::filesystem::exists(m_stateFile))
    {
        std::ifstream inFile(m_stateFile);
        if (inFile.is_open())
        {
            try
            {
                nlohmann::json j;
                inFile >> j;
                
                if (j.contains("total_cost"))
                {
                    m_currentCost = j.at("total_cost").get<double>();
                }
                
                // Session costs are not persisted - they're session-specific
            }
            catch (...)
            {
                // Fallback to old format (just a number)
                inFile.seekg(0);
                inFile >> m_currentCost;
            }
        }
    }
}

void CostManager::saveState()
{
    std::ofstream outFile(m_stateFile);
    if (outFile.is_open())
    {
        nlohmann::json j = {
            {"total_cost", m_currentCost}
        };
        outFile << j.dump(2);
    }
}

} // namespace arbiterAI