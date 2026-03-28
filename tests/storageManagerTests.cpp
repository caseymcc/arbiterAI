#include "arbiterAI/storageManager.h"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

namespace arbiterAI
{

class StorageManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        StorageManager::reset();

        m_testDir="sm_test_models";
        std::filesystem::create_directories(m_testDir);
    }

    void TearDown() override
    {
        StorageManager::instance().shutdown();
        std::filesystem::remove_all(m_testDir);
    }

    void createDummyGguf(const std::string &filename, int64_t sizeBytes)
    {
        std::filesystem::path path=m_testDir/filename;
        std::ofstream out(path, std::ios::binary);
        std::string data(static_cast<size_t>(sizeBytes), '\0');
        out.write(data.data(), sizeBytes);
        out.close();
    }

    std::filesystem::path m_testDir;
};

// ========== Initialization ==========

TEST_F(StorageManagerTest, InitializeCreatesDirectory)
{
    std::filesystem::path subdir=m_testDir/"subdir"/"models";
    StorageManager::instance().initialize(subdir);

    EXPECT_TRUE(std::filesystem::exists(subdir));
}

TEST_F(StorageManagerTest, GetStorageInfoReturnsValidData)
{
    StorageManager::instance().initialize(m_testDir);

    StorageInfo info=StorageManager::instance().getStorageInfo();

    EXPECT_EQ(info.modelsDirectory, m_testDir);
    EXPECT_GT(info.totalDiskBytes, 0);
    EXPECT_GT(info.freeDiskBytes, 0);
    EXPECT_EQ(info.usedByModelsBytes, 0);
    EXPECT_EQ(info.modelCount, 0);
}

// ========== Storage Limit ==========

TEST_F(StorageManagerTest, SetAndGetStorageLimit)
{
    StorageManager::instance().initialize(m_testDir);

    int64_t limit=10LL*1024*1024*1024; // 10 GB
    StorageManager::instance().setStorageLimit(limit);

    EXPECT_EQ(StorageManager::instance().getStorageLimit(), limit);
}

TEST_F(StorageManagerTest, CanDownloadRespectsLimit)
{
    StorageManager::instance().initialize(m_testDir);

    // Set a small limit
    StorageManager::instance().setStorageLimit(100*1024*1024); // 100 MB

    EXPECT_TRUE(StorageManager::instance().canDownload(50*1024*1024));  // 50 MB fits
    EXPECT_FALSE(StorageManager::instance().canDownload(200*1024*1024)); // 200 MB doesn't
}

TEST_F(StorageManagerTest, CanDownloadWithZeroLimitUsesFreeDisk)
{
    StorageManager::instance().initialize(m_testDir);

    // 0 = use all free disk space
    StorageManager::instance().setStorageLimit(0);

    // A small download should always fit
    EXPECT_TRUE(StorageManager::instance().canDownload(1024));
}

// ========== Registration ==========

TEST_F(StorageManagerTest, RegisterDownloadAddsEntry)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "test-q4.gguf", 4370*1024*1024LL);

    std::vector<DownloadedModelFile> models=StorageManager::instance().getDownloadedModels();

    ASSERT_EQ(models.size(), 1u);
    EXPECT_EQ(models[0].modelName, "test-model");
    EXPECT_EQ(models[0].variant, "Q4_K_M");
    EXPECT_EQ(models[0].filename, "test-q4.gguf");
    EXPECT_EQ(models[0].fileSizeBytes, 4370*1024*1024LL);
    EXPECT_EQ(models[0].usageCount, 0);
    EXPECT_FALSE(models[0].hotReady);
    EXPECT_FALSE(models[0].isProtected);
}

TEST_F(StorageManagerTest, RegisterDuplicateUpdatesEntry)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "test-q4.gguf", 4000*1024*1024LL);
    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "test-q4-v2.gguf", 4500*1024*1024LL);

    std::vector<DownloadedModelFile> models=StorageManager::instance().getDownloadedModels();

    ASSERT_EQ(models.size(), 1u);
    EXPECT_EQ(models[0].filename, "test-q4-v2.gguf");
    EXPECT_EQ(models[0].fileSizeBytes, 4500*1024*1024LL);
}

TEST_F(StorageManagerTest, MultipleVariantsTrackedSeparately)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "test-q4.gguf", 4000*1024*1024LL);
    StorageManager::instance().registerDownload("test-model", "Q8_0", "test-q8.gguf", 8000*1024*1024LL);

    std::vector<DownloadedModelFile> models=StorageManager::instance().getDownloadedModels();
    EXPECT_EQ(models.size(), 2u);

    StorageInfo info=StorageManager::instance().getStorageInfo();
    EXPECT_EQ(info.usedByModelsBytes, (4000+8000)*1024*1024LL);
    EXPECT_EQ(info.modelCount, 2);
}

// ========== Usage Tracking ==========

TEST_F(StorageManagerTest, RecordUsageUpdatesCount)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "test-q4.gguf", 1024*1024LL);

    StorageManager::instance().recordUsage("test-model", "Q4_K_M");
    StorageManager::instance().recordUsage("test-model", "Q4_K_M");
    StorageManager::instance().recordUsage("test-model", "Q4_K_M");

    std::optional<DownloadedModelFile> stats=StorageManager::instance().getVariantStats("test-model", "Q4_K_M");

    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->usageCount, 3);
}

TEST_F(StorageManagerTest, RecordUsageUpdatesLastUsedTime)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "test-q4.gguf", 1024*1024LL);

    auto before=std::chrono::system_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    StorageManager::instance().recordUsage("test-model", "Q4_K_M");

    std::optional<DownloadedModelFile> stats=StorageManager::instance().getVariantStats("test-model", "Q4_K_M");

    ASSERT_TRUE(stats.has_value());
    EXPECT_GE(stats->lastUsedAt, before);
}

TEST_F(StorageManagerTest, RecordUsageForUnknownVariantIsNoOp)
{
    StorageManager::instance().initialize(m_testDir);

    // Should not crash
    StorageManager::instance().recordUsage("nonexistent", "Q4_K_M");

    std::vector<DownloadedModelFile> models=StorageManager::instance().getDownloadedModels();
    EXPECT_EQ(models.size(), 0u);
}

// ========== Model/Variant Stats ==========

TEST_F(StorageManagerTest, GetModelStatsReturnsAllVariants)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "q4.gguf", 4000*1024*1024LL);
    StorageManager::instance().registerDownload("test-model", "Q8_0", "q8.gguf", 8000*1024*1024LL);
    StorageManager::instance().registerDownload("other-model", "Q4_K_M", "other.gguf", 3000*1024*1024LL);

    std::vector<DownloadedModelFile> stats=StorageManager::instance().getModelStats("test-model");

    EXPECT_EQ(stats.size(), 2u);
}

TEST_F(StorageManagerTest, GetVariantStatsReturnsNulloptForMissing)
{
    StorageManager::instance().initialize(m_testDir);

    std::optional<DownloadedModelFile> stats=StorageManager::instance().getVariantStats("nonexistent", "Q4_K_M");

    EXPECT_FALSE(stats.has_value());
}

// ========== Hot Ready Flag ==========

TEST_F(StorageManagerTest, SetHotReadyOnVariant)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "q4.gguf", 1024*1024LL);

    bool found=StorageManager::instance().setHotReady("test-model", "Q4_K_M", true);

    EXPECT_TRUE(found);

    std::optional<DownloadedModelFile> stats=StorageManager::instance().getVariantStats("test-model", "Q4_K_M");
    ASSERT_TRUE(stats.has_value());
    EXPECT_TRUE(stats->hotReady);
}

TEST_F(StorageManagerTest, ClearHotReadyOnVariant)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "q4.gguf", 1024*1024LL);
    StorageManager::instance().setHotReady("test-model", "Q4_K_M", true);
    StorageManager::instance().setHotReady("test-model", "Q4_K_M", false);

    std::optional<DownloadedModelFile> stats=StorageManager::instance().getVariantStats("test-model", "Q4_K_M");
    ASSERT_TRUE(stats.has_value());
    EXPECT_FALSE(stats->hotReady);
}

TEST_F(StorageManagerTest, SetHotReadyOnUnknownVariantReturnsFalse)
{
    StorageManager::instance().initialize(m_testDir);

    bool found=StorageManager::instance().setHotReady("nonexistent", "Q4_K_M", true);

    EXPECT_FALSE(found);
}

// ========== Protected Flag ==========

TEST_F(StorageManagerTest, SetProtectedOnVariant)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "q4.gguf", 1024*1024LL);

    bool found=StorageManager::instance().setProtected("test-model", "Q4_K_M", true);

    EXPECT_TRUE(found);

    std::optional<DownloadedModelFile> stats=StorageManager::instance().getVariantStats("test-model", "Q4_K_M");
    ASSERT_TRUE(stats.has_value());
    EXPECT_TRUE(stats->isProtected);
}

TEST_F(StorageManagerTest, ClearProtectedOnVariant)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "q4.gguf", 1024*1024LL);
    StorageManager::instance().setProtected("test-model", "Q4_K_M", true);
    StorageManager::instance().setProtected("test-model", "Q4_K_M", false);

    std::optional<DownloadedModelFile> stats=StorageManager::instance().getVariantStats("test-model", "Q4_K_M");
    ASSERT_TRUE(stats.has_value());
    EXPECT_FALSE(stats->isProtected);
}

// ========== Guarded (hot ready OR protected) ==========

TEST_F(StorageManagerTest, IsGuardedWhenHotReady)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "q4.gguf", 1024*1024LL);
    StorageManager::instance().setHotReady("test-model", "Q4_K_M", true);

    EXPECT_TRUE(StorageManager::instance().isGuarded("test-model", "Q4_K_M"));
}

TEST_F(StorageManagerTest, IsGuardedWhenProtected)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "q4.gguf", 1024*1024LL);
    StorageManager::instance().setProtected("test-model", "Q4_K_M", true);

    EXPECT_TRUE(StorageManager::instance().isGuarded("test-model", "Q4_K_M"));
}

TEST_F(StorageManagerTest, NotGuardedByDefault)
{
    StorageManager::instance().initialize(m_testDir);

    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "q4.gguf", 1024*1024LL);

    EXPECT_FALSE(StorageManager::instance().isGuarded("test-model", "Q4_K_M"));
}

TEST_F(StorageManagerTest, IsGuardedReturnsFalseForUnknown)
{
    StorageManager::instance().initialize(m_testDir);

    EXPECT_FALSE(StorageManager::instance().isGuarded("nonexistent", "Q4_K_M"));
}

// ========== Deletion ==========

TEST_F(StorageManagerTest, DeleteModelFileRemovesFromDisk)
{
    StorageManager::instance().initialize(m_testDir);

    createDummyGguf("test-q4.gguf", 1024);
    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "test-q4.gguf", 1024);

    int64_t freed=0;
    bool result=StorageManager::instance().deleteModelFile("test-model", "Q4_K_M", freed);

    EXPECT_TRUE(result);
    EXPECT_GT(freed, 0);
    EXPECT_FALSE(std::filesystem::exists(m_testDir/"test-q4.gguf"));

    // Entry should be removed
    std::vector<DownloadedModelFile> models=StorageManager::instance().getDownloadedModels();
    EXPECT_EQ(models.size(), 0u);
}

TEST_F(StorageManagerTest, DeleteGuardedHotReadyVariantFails)
{
    StorageManager::instance().initialize(m_testDir);

    createDummyGguf("test-q4.gguf", 1024);
    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "test-q4.gguf", 1024);
    StorageManager::instance().setHotReady("test-model", "Q4_K_M", true);

    int64_t freed=0;
    bool result=StorageManager::instance().deleteModelFile("test-model", "Q4_K_M", freed);

    EXPECT_FALSE(result);
    EXPECT_EQ(freed, 0);
    EXPECT_TRUE(std::filesystem::exists(m_testDir/"test-q4.gguf"));
}

TEST_F(StorageManagerTest, DeleteGuardedProtectedVariantFails)
{
    StorageManager::instance().initialize(m_testDir);

    createDummyGguf("test-q4.gguf", 1024);
    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "test-q4.gguf", 1024);
    StorageManager::instance().setProtected("test-model", "Q4_K_M", true);

    int64_t freed=0;
    bool result=StorageManager::instance().deleteModelFile("test-model", "Q4_K_M", freed);

    EXPECT_FALSE(result);
    EXPECT_EQ(freed, 0);
    EXPECT_TRUE(std::filesystem::exists(m_testDir/"test-q4.gguf"));
}

TEST_F(StorageManagerTest, DeleteNonexistentModelReturnsFalse)
{
    StorageManager::instance().initialize(m_testDir);

    int64_t freed=0;
    bool result=StorageManager::instance().deleteModelFile("nonexistent", "Q4_K_M", freed);

    EXPECT_FALSE(result);
}

// ========== Directory Scanning ==========

TEST_F(StorageManagerTest, ScanPicksUpUntrackedGgufFiles)
{
    createDummyGguf("untracked-model.gguf", 2048);

    StorageManager::instance().initialize(m_testDir);

    std::vector<DownloadedModelFile> models=StorageManager::instance().getDownloadedModels();

    // Should have found the GGUF file
    ASSERT_EQ(models.size(), 1u);
    EXPECT_EQ(models[0].filename, "untracked-model.gguf");
    EXPECT_EQ(models[0].fileSizeBytes, 2048);
}

// ========== Cleanup ==========

TEST_F(StorageManagerTest, SetAndGetCleanupPolicy)
{
    StorageManager::instance().initialize(m_testDir);

    CleanupPolicy policy;
    policy.enabled=true;
    policy.maxAge=std::chrono::hours{14*24}; // 14 days
    policy.checkInterval=std::chrono::hours{12};
    policy.targetFreePercent=30.0;

    StorageManager::instance().setCleanupPolicy(policy);

    CleanupPolicy result=StorageManager::instance().getCleanupPolicy();

    EXPECT_TRUE(result.enabled);
    EXPECT_EQ(result.maxAge.count(), 14*24);
    EXPECT_EQ(result.checkInterval.count(), 12);
    EXPECT_DOUBLE_EQ(result.targetFreePercent, 30.0);
}

TEST_F(StorageManagerTest, PreviewCleanupReturnsNoGuardedCandidates)
{
    StorageManager::instance().initialize(m_testDir);

    createDummyGguf("old-model.gguf", 1024);
    StorageManager::instance().registerDownload("old-model", "Q4_K_M", "old-model.gguf", 1024);
    StorageManager::instance().setProtected("old-model", "Q4_K_M", true);

    std::vector<CleanupCandidate> candidates=StorageManager::instance().previewCleanup();

    // Protected variant should not be a candidate
    for(const CleanupCandidate &c:candidates)
    {
        EXPECT_NE(c.modelName, "old-model");
    }
}

TEST_F(StorageManagerTest, PreviewCleanupReturnsNoHotReadyCandidates)
{
    StorageManager::instance().initialize(m_testDir);

    createDummyGguf("hot-model.gguf", 1024);
    StorageManager::instance().registerDownload("hot-model", "Q4_K_M", "hot-model.gguf", 1024);
    StorageManager::instance().setHotReady("hot-model", "Q4_K_M", true);

    std::vector<CleanupCandidate> candidates=StorageManager::instance().previewCleanup();

    for(const CleanupCandidate &c:candidates)
    {
        EXPECT_NE(c.modelName, "hot-model");
    }
}

TEST_F(StorageManagerTest, RunCleanupDeletesStaleCandidates)
{
    StorageManager::instance().initialize(m_testDir);

    // Set a very short max age so everything qualifies
    CleanupPolicy policy;
    policy.enabled=true;
    policy.maxAge=std::chrono::hours{0};
    policy.targetFreePercent=0.0;
    StorageManager::instance().setCleanupPolicy(policy);

    createDummyGguf("stale.gguf", 2048);
    StorageManager::instance().registerDownload("stale-model", "Q4_K_M", "stale.gguf", 2048);

    int64_t freed=StorageManager::instance().runCleanup();

    EXPECT_GT(freed, 0);
    EXPECT_FALSE(std::filesystem::exists(m_testDir/"stale.gguf"));
}

TEST_F(StorageManagerTest, RunCleanupSkipsGuardedVariants)
{
    StorageManager::instance().initialize(m_testDir);

    CleanupPolicy policy;
    policy.enabled=true;
    policy.maxAge=std::chrono::hours{0};
    policy.targetFreePercent=0.0;
    StorageManager::instance().setCleanupPolicy(policy);

    createDummyGguf("guarded.gguf", 2048);
    StorageManager::instance().registerDownload("guarded-model", "Q4_K_M", "guarded.gguf", 2048);
    StorageManager::instance().setProtected("guarded-model", "Q4_K_M", true);

    int64_t freed=StorageManager::instance().runCleanup();

    EXPECT_EQ(freed, 0);
    EXPECT_TRUE(std::filesystem::exists(m_testDir/"guarded.gguf"));
}

// ========== Persistence ==========

TEST_F(StorageManagerTest, FlushAndReloadPreservesData)
{
    // Initialize, register, set flags, flush
    {
        StorageManager::instance().initialize(m_testDir);
        createDummyGguf("test-q4.gguf", 4000);
        StorageManager::instance().registerDownload("test-model", "Q4_K_M", "test-q4.gguf", 4000LL);
        StorageManager::instance().recordUsage("test-model", "Q4_K_M");
        StorageManager::instance().recordUsage("test-model", "Q4_K_M");
        StorageManager::instance().setHotReady("test-model", "Q4_K_M", true);
        StorageManager::instance().setProtected("test-model", "Q4_K_M", true);
        StorageManager::instance().flush();
    }

    // Reset and reinitialize — should reload from disk
    StorageManager::reset();
    StorageManager::instance().initialize(m_testDir);

    std::optional<DownloadedModelFile> stats=StorageManager::instance().getVariantStats("test-model", "Q4_K_M");

    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->modelName, "test-model");
    EXPECT_EQ(stats->variant, "Q4_K_M");
    EXPECT_EQ(stats->filename, "test-q4.gguf");
    EXPECT_EQ(stats->usageCount, 2);
    EXPECT_TRUE(stats->hotReady);
    EXPECT_TRUE(stats->isProtected);
}

// ========== Delete All Variants of a Model ==========

TEST_F(StorageManagerTest, DeleteAllVariantsOfModel)
{
    StorageManager::instance().initialize(m_testDir);

    createDummyGguf("q4.gguf", 1024);
    createDummyGguf("q8.gguf", 2048);
    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "q4.gguf", 1024);
    StorageManager::instance().registerDownload("test-model", "Q8_0", "q8.gguf", 2048);

    int64_t freed=0;
    bool result=StorageManager::instance().deleteModelFile("test-model", "", freed);

    EXPECT_TRUE(result);
    EXPECT_GT(freed, 0);
    EXPECT_FALSE(std::filesystem::exists(m_testDir/"q4.gguf"));
    EXPECT_FALSE(std::filesystem::exists(m_testDir/"q8.gguf"));

    std::vector<DownloadedModelFile> models=StorageManager::instance().getDownloadedModels();
    EXPECT_EQ(models.size(), 0u);
}

TEST_F(StorageManagerTest, DeleteAllVariantsFailsIfAnyGuarded)
{
    StorageManager::instance().initialize(m_testDir);

    createDummyGguf("q4.gguf", 1024);
    createDummyGguf("q8.gguf", 2048);
    StorageManager::instance().registerDownload("test-model", "Q4_K_M", "q4.gguf", 1024);
    StorageManager::instance().registerDownload("test-model", "Q8_0", "q8.gguf", 2048);
    StorageManager::instance().setProtected("test-model", "Q8_0", true);

    int64_t freed=0;
    bool result=StorageManager::instance().deleteModelFile("test-model", "", freed);

    EXPECT_FALSE(result);
    // Both files should still exist since one is guarded
    EXPECT_TRUE(std::filesystem::exists(m_testDir/"q4.gguf"));
    EXPECT_TRUE(std::filesystem::exists(m_testDir/"q8.gguf"));
}

} // namespace arbiterAI
