#ifndef _ARBITER_AI_COST_MANAGER_H_
#define _ARBITER_AI_COST_MANAGER_H_

#include <filesystem>

namespace arbiterAI
{

class CostManager
{
public:
    CostManager(std::filesystem::path stateFile, double spendingLimit);

    bool canProceed(double estimatedCost);
    void recordCost(double cost);

private:
    std::filesystem::path m_stateFile;
    double m_spendingLimit;
    double m_currentCost;

    void loadState();
    void saveState();
};

} // namespace arbiterAI

#endif // _ARBITER_AI_COST_MANAGER_H_