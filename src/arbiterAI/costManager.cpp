#include "arbiterAI/costManager.h"

#include <fstream>
#include <iostream>

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
    return m_currentCost+estimatedCost<=m_spendingLimit;
}

void CostManager::recordCost(double cost)
{
    m_currentCost+=cost;
    saveState();
}

void CostManager::loadState()
{
    if(std::filesystem::exists(m_stateFile))
    {
        std::ifstream inFile(m_stateFile);
        if(inFile.is_open())
        {
            inFile>>m_currentCost;
        }
    }
}

void CostManager::saveState()
{
    std::ofstream outFile(m_stateFile);
    if(outFile.is_open())
    {
        outFile<<m_currentCost;
    }
}

} // namespace arbiterAI