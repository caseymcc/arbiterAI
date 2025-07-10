#include "hermesaxiom/modelDownloader.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <memory>
#include "hermesaxiom/fileVerifier.h"
#include <httplib.h>
#include <thread>

namespace hermesaxiom
{

class MockFileVerifier : public IFileVerifier {
public:
    MOCK_METHOD(bool, verifyFile, (const std::string&, const std::string&), (override));
};

class ModelDownloaderTest : public ::testing::Test
{
protected:
    httplib::Server svr;
    std::thread svr_thread;

    void SetUp() override
    {
        // Create a dummy file to be "downloaded"
        std::ofstream outfile("dummy_model.bin");
        outfile << "This is a test model file.";
        outfile.close();

        svr.Get("/dummy_model.bin", [](const httplib::Request &, httplib::Response &res) {
            std::ifstream file("dummy_model.bin", std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            res.set_content(content, "application/octet-stream");
        });

        svr_thread = std::thread([&]() {
            svr.listen("localhost", 1234);
        });
    }

    void TearDown() override
    {
        svr.stop();
        svr_thread.join();
        // Clean up the dummy file
        std::remove("dummy_model.bin");
    }
};

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

} // namespace hermesaxiom