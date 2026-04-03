#ifndef _ARBITERAI_TELEMETRYCOLLECTOR_H_
#define _ARBITERAI_TELEMETRYCOLLECTOR_H_

#include "arbiterAI/hardwareDetector.h"
#include "arbiterAI/modelRuntime.h"

#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <deque>

namespace arbiterAI
{

struct InferenceStats {
    std::string model;
    std::string variant;
    double tokensPerSecond=0.0;
    double promptTokensPerSecond=0.0;     // prompt processing speed (tokens in / sec)
    double generationTokensPerSecond=0.0; // generation speed (tokens out / sec)
    int promptTokens=0;
    int completionTokens=0;
    double latencyMs=0.0;      // time to first token
    double totalTimeMs=0.0;    // total request time
    double promptTimeMs=0.0;   // time spent processing prompt
    double generationTimeMs=0.0; // time spent generating tokens
    std::chrono::system_clock::time_point timestamp;
};

struct SwapEvent {
    std::string from;
    std::string to;
    double timeMs=0.0;
    std::chrono::system_clock::time_point when;
};

struct SystemSnapshot {
    SystemInfo hardware;
    std::vector<LoadedModel> models;
    double avgTokensPerSecond=0.0;
    double avgPromptTokensPerSecond=0.0;
    double avgGenerationTokensPerSecond=0.0;
    int activeRequests=0;
};

class TelemetryCollector {
public:
    static TelemetryCollector &instance();
    static void reset(); // For testing

    /// Record an inference event
    void recordInference(const InferenceStats &stats);

    /// Record a model swap event
    void recordModelSwap(const std::string &from, const std::string &to, double swapTimeMs);

    /// Get current system snapshot
    SystemSnapshot getSnapshot() const;

    /// Get inference history within the given time window
    std::vector<InferenceStats> getHistory(std::chrono::minutes window) const;

    /// Get all recorded swap events
    std::vector<SwapEvent> getSwapHistory() const;

    /// Get the rolling average tokens/sec across recent inferences
    double getAvgTokensPerSecond() const;

    /// Get the total number of recorded inference events
    size_t getInferenceCount() const;

    /// Get the total number of recorded swap events
    size_t getSwapCount() const;

private:
    TelemetryCollector()=default;

    TelemetryCollector(const TelemetryCollector &)=delete;
    TelemetryCollector &operator=(const TelemetryCollector &)=delete;

    /// Prune inference history older than the max retention window
    void pruneHistory() const;

    static constexpr int MAX_INFERENCE_HISTORY=10000;
    static constexpr int MAX_SWAP_HISTORY=1000;
    static constexpr std::chrono::minutes MAX_RETENTION{60};

    mutable std::mutex m_mutex;
    mutable std::deque<InferenceStats> m_inferenceHistory;
    std::deque<SwapEvent> m_swapHistory;
};

} // namespace arbiterAI

#endif//_ARBITERAI_TELEMETRYCOLLECTOR_H_
