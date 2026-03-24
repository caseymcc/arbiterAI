#include "arbiterAI/telemetryCollector.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/hardwareDetector.h"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <thread>

namespace arbiterAI
{

class TelemetryCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        TelemetryCollector::reset();
        ModelRuntime::reset();
        ModelManager::reset();

        // Set up minimal config for ModelRuntime tests that need it
        std::filesystem::create_directories("tel_config/models");

        std::ofstream config("tel_config/models/mock.json");
        config << R"({
            "models": [
                {
                    "model": "tel-mock-1",
                    "provider": "mock",
                    "ranking": 80,
                    "context_window": 8192,
                    "max_tokens": 4096
                },
                {
                    "model": "tel-mock-2",
                    "provider": "mock",
                    "ranking": 70,
                    "context_window": 4096,
                    "max_tokens": 2048
                }
            ]
        })";
        config.close();

        ModelManager::instance().initialize({"tel_config"});
    }

    void TearDown() override
    {
        std::filesystem::remove_all("tel_config");
    }

    InferenceStats makeStats(
        const std::string &model,
        double tps,
        int promptTokens,
        int completionTokens,
        double latencyMs,
        double totalTimeMs)
    {
        InferenceStats stats;
        stats.model=model;
        stats.variant="Q4_K_M";
        stats.tokensPerSecond=tps;
        stats.promptTokens=promptTokens;
        stats.completionTokens=completionTokens;
        stats.latencyMs=latencyMs;
        stats.totalTimeMs=totalTimeMs;
        stats.timestamp=std::chrono::system_clock::now();
        return stats;
    }
};

// --- Record and retrieve inference stats ---

TEST_F(TelemetryCollectorTest, RecordInferenceIncrementsCount)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    EXPECT_EQ(tc.getInferenceCount(), 0u);

    tc.recordInference(makeStats("model-a", 50.0, 100, 200, 10.0, 500.0));

    EXPECT_EQ(tc.getInferenceCount(), 1u);
}

TEST_F(TelemetryCollectorTest, RecordMultipleInferences)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    tc.recordInference(makeStats("model-a", 50.0, 100, 200, 10.0, 500.0));
    tc.recordInference(makeStats("model-b", 75.0, 150, 300, 8.0, 400.0));
    tc.recordInference(makeStats("model-a", 60.0, 120, 240, 12.0, 600.0));

    EXPECT_EQ(tc.getInferenceCount(), 3u);
}

TEST_F(TelemetryCollectorTest, GetHistoryReturnsRecentEntries)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    tc.recordInference(makeStats("model-a", 50.0, 100, 200, 10.0, 500.0));
    tc.recordInference(makeStats("model-b", 75.0, 150, 300, 8.0, 400.0));

    std::vector<InferenceStats> history=tc.getHistory(std::chrono::minutes(5));

    EXPECT_EQ(history.size(), 2u);
    EXPECT_EQ(history[0].model, "model-a");
    EXPECT_EQ(history[1].model, "model-b");
}

TEST_F(TelemetryCollectorTest, GetHistoryFiltersOldEntries)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    // Create an entry with an old timestamp
    InferenceStats oldStats=makeStats("old-model", 30.0, 50, 100, 20.0, 1000.0);
    oldStats.timestamp=std::chrono::system_clock::now()-std::chrono::minutes(10);
    tc.recordInference(oldStats);

    // Create a recent entry
    tc.recordInference(makeStats("new-model", 60.0, 100, 200, 10.0, 500.0));

    std::vector<InferenceStats> history=tc.getHistory(std::chrono::minutes(5));

    EXPECT_EQ(history.size(), 1u);
    EXPECT_EQ(history[0].model, "new-model");
}

TEST_F(TelemetryCollectorTest, GetHistoryFieldsPreserved)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    tc.recordInference(makeStats("model-x", 42.5, 128, 256, 15.0, 700.0));

    std::vector<InferenceStats> history=tc.getHistory(std::chrono::minutes(5));

    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history[0].model, "model-x");
    EXPECT_EQ(history[0].variant, "Q4_K_M");
    EXPECT_DOUBLE_EQ(history[0].tokensPerSecond, 42.5);
    EXPECT_EQ(history[0].promptTokens, 128);
    EXPECT_EQ(history[0].completionTokens, 256);
    EXPECT_DOUBLE_EQ(history[0].latencyMs, 15.0);
    EXPECT_DOUBLE_EQ(history[0].totalTimeMs, 700.0);
}

// --- Swap events ---

TEST_F(TelemetryCollectorTest, RecordSwapIncrementsCount)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    EXPECT_EQ(tc.getSwapCount(), 0u);

    tc.recordModelSwap("model-a", "model-b", 150.0);

    EXPECT_EQ(tc.getSwapCount(), 1u);
}

TEST_F(TelemetryCollectorTest, GetSwapHistoryReturnsAll)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    tc.recordModelSwap("model-a", "model-b", 150.0);
    tc.recordModelSwap("model-b", "model-c", 200.0);

    std::vector<SwapEvent> swaps=tc.getSwapHistory();

    ASSERT_EQ(swaps.size(), 2u);
    EXPECT_EQ(swaps[0].from, "model-a");
    EXPECT_EQ(swaps[0].to, "model-b");
    EXPECT_DOUBLE_EQ(swaps[0].timeMs, 150.0);
    EXPECT_EQ(swaps[1].from, "model-b");
    EXPECT_EQ(swaps[1].to, "model-c");
    EXPECT_DOUBLE_EQ(swaps[1].timeMs, 200.0);
}

TEST_F(TelemetryCollectorTest, SwapEventTimestampSet)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    std::chrono::system_clock::time_point before=std::chrono::system_clock::now();
    tc.recordModelSwap("a", "b", 100.0);
    std::chrono::system_clock::time_point after=std::chrono::system_clock::now();

    std::vector<SwapEvent> swaps=tc.getSwapHistory();
    ASSERT_EQ(swaps.size(), 1u);
    EXPECT_GE(swaps[0].when, before);
    EXPECT_LE(swaps[0].when, after);
}

// --- Rolling average ---

TEST_F(TelemetryCollectorTest, AvgTokensPerSecondEmpty)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    EXPECT_DOUBLE_EQ(tc.getAvgTokensPerSecond(), 0.0);
}

TEST_F(TelemetryCollectorTest, AvgTokensPerSecondSingleEntry)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    tc.recordInference(makeStats("model-a", 50.0, 100, 200, 10.0, 500.0));

    EXPECT_DOUBLE_EQ(tc.getAvgTokensPerSecond(), 50.0);
}

TEST_F(TelemetryCollectorTest, AvgTokensPerSecondMultipleEntries)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    tc.recordInference(makeStats("model-a", 40.0, 100, 200, 10.0, 500.0));
    tc.recordInference(makeStats("model-a", 60.0, 100, 200, 10.0, 500.0));

    EXPECT_DOUBLE_EQ(tc.getAvgTokensPerSecond(), 50.0);
}

TEST_F(TelemetryCollectorTest, AvgTokensPerSecondIgnoresZero)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    tc.recordInference(makeStats("model-a", 50.0, 100, 200, 10.0, 500.0));
    tc.recordInference(makeStats("model-a", 0.0, 0, 0, 0.0, 0.0));

    // Should only average non-zero entries
    EXPECT_DOUBLE_EQ(tc.getAvgTokensPerSecond(), 50.0);
}

// --- Snapshot ---

TEST_F(TelemetryCollectorTest, SnapshotContainsHardwareInfo)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    SystemSnapshot snapshot=tc.getSnapshot();

    EXPECT_GT(snapshot.hardware.totalRamMb, 0);
    EXPECT_GT(snapshot.hardware.cpuCores, 0);
}

TEST_F(TelemetryCollectorTest, SnapshotContainsLoadedModels)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    ModelRuntime::instance().loadModel("tel-mock-1");

    SystemSnapshot snapshot=tc.getSnapshot();

    EXPECT_GE(snapshot.models.size(), 1u);
}

TEST_F(TelemetryCollectorTest, SnapshotActiveRequests)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    SystemSnapshot snapshot1=tc.getSnapshot();
    EXPECT_EQ(snapshot1.activeRequests, 0);

    ModelRuntime::instance().loadModel("tel-mock-1");
    ModelRuntime::instance().beginInference("tel-mock-1");

    SystemSnapshot snapshot2=tc.getSnapshot();
    EXPECT_EQ(snapshot2.activeRequests, 1);

    ModelRuntime::instance().endInference();

    SystemSnapshot snapshot3=tc.getSnapshot();
    EXPECT_EQ(snapshot3.activeRequests, 0);
}

TEST_F(TelemetryCollectorTest, SnapshotAvgTokensPerSecond)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    tc.recordInference(makeStats("model-a", 40.0, 100, 200, 10.0, 500.0));
    tc.recordInference(makeStats("model-a", 60.0, 100, 200, 10.0, 500.0));

    SystemSnapshot snapshot=tc.getSnapshot();

    EXPECT_DOUBLE_EQ(snapshot.avgTokensPerSecond, 50.0);
}

// --- Reset ---

TEST_F(TelemetryCollectorTest, ResetClearsAll)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    tc.recordInference(makeStats("model-a", 50.0, 100, 200, 10.0, 500.0));
    tc.recordModelSwap("a", "b", 100.0);

    EXPECT_EQ(tc.getInferenceCount(), 1u);
    EXPECT_EQ(tc.getSwapCount(), 1u);

    TelemetryCollector::reset();

    EXPECT_EQ(tc.getInferenceCount(), 0u);
    EXPECT_EQ(tc.getSwapCount(), 0u);
}

// --- Integration: ModelRuntime swap records telemetry ---

TEST_F(TelemetryCollectorTest, SwapModelRecordsTelemetry)
{
    TelemetryCollector &tc=TelemetryCollector::instance();

    ModelRuntime::instance().loadModel("tel-mock-1");
    ModelRuntime::instance().swapModel("tel-mock-2");

    EXPECT_EQ(tc.getSwapCount(), 1u);

    std::vector<SwapEvent> swaps=tc.getSwapHistory();
    ASSERT_EQ(swaps.size(), 1u);
    EXPECT_EQ(swaps[0].from, "tel-mock-1");
    EXPECT_EQ(swaps[0].to, "tel-mock-2");
    EXPECT_GT(swaps[0].timeMs, 0.0);
}

} // namespace arbiterAI
