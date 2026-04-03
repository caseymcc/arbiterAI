#include "arbiterAI/telemetryCollector.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <numeric>

namespace arbiterAI
{

TelemetryCollector &TelemetryCollector::instance()
{
    static TelemetryCollector collector;
    return collector;
}

void TelemetryCollector::reset()
{
    TelemetryCollector &tc=instance();
    std::lock_guard<std::mutex> lock(tc.m_mutex);
    tc.m_inferenceHistory.clear();
    tc.m_swapHistory.clear();
}

void TelemetryCollector::recordInference(const InferenceStats &stats)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_inferenceHistory.push_back(stats);

    // Cap history size
    while(m_inferenceHistory.size()>MAX_INFERENCE_HISTORY)
    {
        m_inferenceHistory.pop_front();
    }

    spdlog::debug("Recorded inference: model='{}' variant='{}' tps={:.1f} prompt={} completion={} latency={:.1f}ms total={:.1f}ms",
        stats.model, stats.variant, stats.tokensPerSecond,
        stats.promptTokens, stats.completionTokens,
        stats.latencyMs, stats.totalTimeMs);
}

void TelemetryCollector::recordModelSwap(const std::string &from, const std::string &to, double swapTimeMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    SwapEvent event;
    event.from=from;
    event.to=to;
    event.timeMs=swapTimeMs;
    event.when=std::chrono::system_clock::now();

    m_swapHistory.push_back(event);

    // Cap history size
    while(m_swapHistory.size()>MAX_SWAP_HISTORY)
    {
        m_swapHistory.pop_front();
    }

    spdlog::info("Model swap: '{}' -> '{}' ({:.1f}ms)", from, to, swapTimeMs);
}

SystemSnapshot TelemetryCollector::getSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    SystemSnapshot snapshot;
    snapshot.hardware=HardwareDetector::instance().getSystemInfo();
    snapshot.models=ModelRuntime::instance().getModelStates();
    snapshot.avgTokensPerSecond=getAvgTokensPerSecond();
    snapshot.activeRequests=ModelRuntime::instance().isInferenceActive()?1:0;

    // Calculate average prompt/generation speeds over last 5 minutes
    std::chrono::system_clock::time_point cutoff=
        std::chrono::system_clock::now()-std::chrono::minutes(5);
    double promptSum=0.0, genSum=0.0;
    int promptCount=0, genCount=0;

    for(const InferenceStats &stat:m_inferenceHistory)
    {
        if(stat.timestamp>=cutoff)
        {
            if(stat.promptTokensPerSecond>0.0)
            {
                promptSum+=stat.promptTokensPerSecond;
                promptCount++;
            }
            if(stat.generationTokensPerSecond>0.0)
            {
                genSum+=stat.generationTokensPerSecond;
                genCount++;
            }
        }
    }

    snapshot.avgPromptTokensPerSecond=promptCount>0?(promptSum/promptCount):0.0;
    snapshot.avgGenerationTokensPerSecond=genCount>0?(genSum/genCount):0.0;

    return snapshot;
}

std::vector<InferenceStats> TelemetryCollector::getHistory(std::chrono::minutes window) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    pruneHistory();

    std::vector<InferenceStats> result;
    std::chrono::system_clock::time_point cutoff=std::chrono::system_clock::now()-window;

    for(const InferenceStats &stat:m_inferenceHistory)
    {
        if(stat.timestamp>=cutoff)
        {
            result.push_back(stat);
        }
    }

    return result;
}

std::vector<SwapEvent> TelemetryCollector::getSwapHistory() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return std::vector<SwapEvent>(m_swapHistory.begin(), m_swapHistory.end());
}

double TelemetryCollector::getAvgTokensPerSecond() const
{
    // Calculate rolling average over recent entries (last 5 minutes)
    std::chrono::system_clock::time_point cutoff=
        std::chrono::system_clock::now()-std::chrono::minutes(5);

    double sum=0.0;
    int count=0;

    for(const InferenceStats &stat:m_inferenceHistory)
    {
        if(stat.timestamp>=cutoff&&stat.tokensPerSecond>0.0)
        {
            sum+=stat.tokensPerSecond;
            count++;
        }
    }

    if(count==0)
    {
        return 0.0;
    }

    return sum/count;
}

size_t TelemetryCollector::getInferenceCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inferenceHistory.size();
}

size_t TelemetryCollector::getSwapCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_swapHistory.size();
}

void TelemetryCollector::pruneHistory() const
{
    // Remove entries older than MAX_RETENTION
    std::chrono::system_clock::time_point cutoff=
        std::chrono::system_clock::now()-MAX_RETENTION;

    while(!m_inferenceHistory.empty()&&m_inferenceHistory.front().timestamp<cutoff)
    {
        m_inferenceHistory.pop_front();
    }
}

} // namespace arbiterAI
