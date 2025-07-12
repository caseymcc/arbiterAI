#include "arbiterAI/modelDownloader.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <memory>
#include "arbiterAI/fileVerifier.h"
#include <httplib.h>
#include <thread>

namespace arbiterAI
{

class MockFileVerifier : public IFileVerifier {
public:
    MOCK_METHOD(bool, verifyFile, (const std::string&, const std::string&), (override));
};

class ModelDownloaderTest : public ::testing::Test
{
protected:
    std::unique_ptr<httplib::Server> svr;
    std::unique_ptr<std::thread> svr_thread;

    void SetUp() override
    {
        svr = std::make_unique<httplib::Server>();
        // Create a dummy file to be "downloaded"
        std::ofstream outfile("dummy_model.bin");
        outfile << "This is a test model file.";
        outfile.close();

        svr->Get("/dummy_model.bin", [](const httplib::Request &, httplib::Response &res) {
            std::ifstream file("dummy_model.bin", std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            res.set_content(content, "application/octet-stream");
        });

        svr_thread = std::make_unique<std::thread>([&]() {
            svr->listen("localhost", 1234);
        });

        {
            //dummy request to ensure server is running
            httplib::Client cli("localhost", 1234);
            cli.Get("/dummy_model.bin"); 
        }
    }

    void TearDown() override
    {
        if (svr && svr->is_running())
        {
            svr->stop();
        }
        if (svr_thread && svr_thread->joinable())
        {
            svr_thread->join();
        }
    std::remove("dummy_model.bin");
    std::filesystem::remove_all("cache_dir");
}
};

// TEST_F(ModelDownloaderTest, DownloadConfigFromRepo)
// {
//     ModelDownloader downloader;
//     svr.Get("/repos/owner/repo/contents/config.json", [](const httplib::Request&, httplib::Response &res) {
//         nlohmann::json j = {
//             {"content", "ewogICAgIm1vZGVsIjogImR1bW15LW1vZGVsIiwKICAgICJwcmljaW5nIjogMAp9"} // base64 of {"model": "dummy-model", "pricing": 0}
//         };
//         res.set_content(j.dump(), "application/json");
//     });

//     std::future<std::optional<nlohmann::json>> result = downloader.downloadConfigFromRepo("owner", "repo", "config.json");
//     auto config = result.get();

//     EXPECT_TRUE(config.has_value());
//     EXPECT_EQ(config.value()["model"], "dummy-model");
// }

TEST_F(ModelDownloaderTest, ParseConfigFromJSON)
{
    ModelDownloader downloader;
    std::string validJson = R"({"model": "test-model"})";
    std::string invalidJson = R"({"model": "test-model")"; // Missing closing brace

    auto config = downloader.parseConfigFromJSON(validJson);
    EXPECT_TRUE(config.has_value());
    EXPECT_EQ(config.value()["model"], "test-model");

    auto invalidConfig = downloader.parseConfigFromJSON(invalidJson);
    EXPECT_FALSE(invalidConfig.has_value());
}

TEST_F(ModelDownloaderTest, DownloadModelWithCorrectHash)
{
    auto mockVerifier = std::make_shared<MockFileVerifier>();
    ModelDownloader downloader(mockVerifier);
    std::string url = "http://localhost:1234/dummy_model.bin";
    std::string filePath = "/tmp/downloaded_model.bin";
    std::string hash = "a7e7a3e3e7b3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3"; // Dummy hash

    EXPECT_CALL(*mockVerifier, verifyFile(testing::_, testing::_))
        .WillOnce(testing::Return(true));

    std::future<bool> result = downloader.downloadModel(url, filePath, hash);
    EXPECT_TRUE(result.get());

    // Clean up the downloaded file
    std::remove(filePath.c_str());
}

TEST_F(ModelDownloaderTest, DownloadModelWithIncorrectHash)
{
    auto mockVerifier = std::make_shared<MockFileVerifier>();
    ModelDownloader downloader(mockVerifier);
    std::string url = "http://localhost:1234/dummy_model.bin";
    std::string filePath = "/tmp/downloaded_model.bin";
    std::string hash = "incorrect_hash";

    EXPECT_CALL(*mockVerifier, verifyFile(testing::_, testing::_))
        .WillOnce(testing::Return(false));

    std::future<bool> result = downloader.downloadModel(url, filePath, hash);
    EXPECT_FALSE(result.get());

    // Clean up the downloaded file
    std::remove(filePath.c_str());
}

TEST_F(ModelDownloaderTest, DownloadModelWithInvalidUrl)
{
    ModelDownloader downloader;
    std::string url = "http://localhost:1235/non_existent_model.bin";
    std::string filePath = "/tmp/downloaded_model.bin";
    std::string hash = "a7e7a3e3e7b3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3";

    std::future<bool> result = downloader.downloadModel(url, filePath, hash);
    EXPECT_FALSE(result.get());
}

TEST_F(ModelDownloaderTest, DownloadModelFailsWithTooLowClientVersion)
{
    ModelDownloader downloader;
    std::string url = "http://localhost:1234/dummy_model.bin";
    std::string filePath = "/tmp/downloaded_model.bin";
    std::string hash = "a7e7a3e3e7b3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3";

    std::future<bool> result = downloader.downloadModel(url, filePath, hash, "2.0.0", "3.0.0");
    EXPECT_FALSE(result.get());
}

TEST_F(ModelDownloaderTest, DownloadModelFailsWithTooHighClientVersion)
{
    ModelDownloader downloader;
    std::string url = "http://localhost:1234/dummy_model.bin";
    std::string filePath = "/tmp/downloaded_model.bin";
    std::string hash = "a7e7a3e3e7b3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3e3";

    std::future<bool> result = downloader.downloadModel(url, filePath, hash, "0.5.0", "0.9.0");
    EXPECT_FALSE(result.get());
}

} // namespace arbiterAI