#include "dashboardAssets.h"
#include "dashboardAssetTable.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <memory>
#include <string>
#include <thread>

namespace
{

std::string bytes(const unsigned char *data, size_t size)
{
    return std::string(reinterpret_cast<const char *>(data), size);
}

/// The dashboard's routes on a server of their own, on a free port.
class DashboardServingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_server=std::make_unique<httplib::Server>();
        arbiterAI::server::registerDashboardRoutes(*m_server);
        m_port=m_server->bind_to_any_port("127.0.0.1");
        ASSERT_GT(m_port, 0);
        m_thread=std::thread([this]() { m_server->listen_after_bind(); });
        m_server->wait_until_ready();

        m_client=std::make_unique<httplib::Client>("127.0.0.1", m_port);
        // the bytes as sent: the client must not undo an encoding
        m_client->set_decompress(false);
    }

    void TearDown() override
    {
        m_server->stop();
        if(m_thread.joinable())
            m_thread.join();
    }

    httplib::Result get(const std::string &path, const httplib::Headers &headers={})
    {
        return m_client->Get(path, headers);
    }

    std::unique_ptr<httplib::Server> m_server;
    std::unique_ptr<httplib::Client> m_client;
    std::thread m_thread;
    int m_port=0;
};

} // namespace

TEST(DashboardHeaders, AcceptsGzipOnlyWhenListedWithoutQZero)
{
    using arbiterAI::server::acceptsGzip;

    EXPECT_TRUE(acceptsGzip("gzip, deflate, br"));
    EXPECT_TRUE(acceptsGzip("br;q=1.0, GZIP;q=0.5"));
    EXPECT_TRUE(acceptsGzip("*"));
    EXPECT_FALSE(acceptsGzip(""));
    EXPECT_FALSE(acceptsGzip("br"));
    EXPECT_FALSE(acceptsGzip("deflate, br, zstd"));
    EXPECT_FALSE(acceptsGzip("gzip;q=0"));
    EXPECT_FALSE(acceptsGzip("x-gzip-like"));
}

TEST(DashboardHeaders, EtagMatchesAnyListedOrWeakOrStar)
{
    using arbiterAI::server::etagMatches;

    EXPECT_TRUE(etagMatches("\"abc\"", "\"abc\""));
    EXPECT_TRUE(etagMatches("\"x\", \"abc\"", "\"abc\""));
    EXPECT_TRUE(etagMatches("W/\"abc\"", "\"abc\""));
    EXPECT_TRUE(etagMatches("*", "\"abc\""));
    EXPECT_FALSE(etagMatches("", "\"abc\""));
    EXPECT_FALSE(etagMatches("\"abd\"", "\"abc\""));
}

TEST(DashboardAssetHandler, UnknownAssetIsNotFound)
{
    httplib::Request req;
    httplib::Response res;

    arbiterAI::server::serveDashboardAsset(req, res, "app.exe");
    EXPECT_EQ(res.status, 404);
    EXPECT_FALSE(res.has_header("ETag"));
}

#ifdef ARBITERAI_DASHBOARD_ASSET_DIR

TEST_F(DashboardServingTest, ShellIsServedAtDashboard)
{
    const arbiterAI::server::DashboardAsset *shell=arbiterAI::server::findDashboardAsset("shell.html");
    ASSERT_NE(shell, nullptr);

    httplib::Result result=get("/dashboard");
    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 200);
    EXPECT_EQ(result->get_header_value("Content-Type"), "text/html; charset=utf-8");
    EXPECT_FALSE(result->has_header("Content-Encoding"));
    EXPECT_EQ(result->body, bytes(shell->data, shell->size));
}

TEST_F(DashboardServingTest, ShellIsServedAtEachTabPath)
{
    const arbiterAI::server::DashboardAsset *shell=arbiterAI::server::findDashboardAsset("shell.html");
    ASSERT_NE(shell, nullptr);

    for(const char *path:{"/dashboard/storage", "/dashboard/config", "/dashboard/logs", "/dashboard/"})
    {
        httplib::Result result=get(path);
        ASSERT_TRUE(result)<<path;
        EXPECT_EQ(result->status, 200)<<path;
        EXPECT_EQ(result->body, bytes(shell->data, shell->size))<<path;
    }
}

TEST_F(DashboardServingTest, TheInterimNextPathIsGone)
{
    for(const char *path:{"/dashboard/next", "/dashboard/next/storage", "/dashboard/other"})
    {
        httplib::Result result=get(path);
        ASSERT_TRUE(result)<<path;
        EXPECT_EQ(result->status, 404)<<path;
    }
}

TEST_F(DashboardServingTest, AssetsHaveTheirMimeTypes)
{
    httplib::Result wasm=get("/dashboard/app.wasm");
    ASSERT_TRUE(wasm);
    EXPECT_EQ(wasm->status, 200);
    EXPECT_EQ(wasm->get_header_value("Content-Type"), "application/wasm");

    httplib::Result js=get("/dashboard/app.js");
    ASSERT_TRUE(js);
    EXPECT_EQ(js->get_header_value("Content-Type"), "application/javascript");

    httplib::Result data=get("/dashboard/app.data");
    ASSERT_TRUE(data);
    EXPECT_EQ(data->get_header_value("Content-Type"), "application/octet-stream");
}

TEST_F(DashboardServingTest, BrowserAcceptEncodingGetsTheGzipCopyOnce)
{
    for(const char *name:{"shell.html", "app.js", "app.wasm"})
    {
        const arbiterAI::server::DashboardAsset *asset=arbiterAI::server::findDashboardAsset(name);
        ASSERT_NE(asset, nullptr)<<name;

        std::string path=std::string(name)=="shell.html"?"/dashboard":std::string("/dashboard/")+name;
        httplib::Result result=get(path, {{"Accept-Encoding", "gzip, deflate, br"}});
        ASSERT_TRUE(result)<<name;
        EXPECT_EQ(result->status, 200)<<name;
        EXPECT_EQ(result->get_header_value_count("Content-Encoding"), 1u)<<name;
        EXPECT_EQ(result->get_header_value("Content-Encoding"), "gzip")<<name;
        EXPECT_EQ(result->body.size(), asset->gzipSize)<<name;
        EXPECT_TRUE(result->body==bytes(asset->gzipData, asset->gzipSize))<<name;
    }
}

TEST_F(DashboardServingTest, RawWhenGzipIsNotAccepted)
{
    const arbiterAI::server::DashboardAsset *js=arbiterAI::server::findDashboardAsset("app.js");
    ASSERT_NE(js, nullptr);

    for(const char *acceptEncoding:{"br", "gzip;q=0"})
    {
        httplib::Result result=get("/dashboard/app.js", {{"Accept-Encoding", acceptEncoding}});
        ASSERT_TRUE(result)<<acceptEncoding;
        EXPECT_FALSE(result->has_header("Content-Encoding"))<<acceptEncoding;
        EXPECT_EQ(result->body.size(), js->size)<<acceptEncoding;
        EXPECT_TRUE(result->body==bytes(js->data, js->size))<<acceptEncoding;
    }
}

TEST_F(DashboardServingTest, EtagAndNoCacheOnEveryAsset)
{
    for(const char *name:{"app.js", "app.wasm", "app.data"})
    {
        const arbiterAI::server::DashboardAsset *asset=arbiterAI::server::findDashboardAsset(name);
        ASSERT_NE(asset, nullptr)<<name;

        httplib::Result result=get(std::string("/dashboard/")+name);
        ASSERT_TRUE(result)<<name;
        EXPECT_EQ(result->get_header_value("ETag"), asset->etag)<<name;
        EXPECT_EQ(result->get_header_value("Cache-Control"), "no-cache")<<name;
        EXPECT_EQ(result->get_header_value("Vary"), "Accept-Encoding")<<name;
    }
}

TEST_F(DashboardServingTest, MatchingIfNoneMatchGets304)
{
    const arbiterAI::server::DashboardAsset *wasm=arbiterAI::server::findDashboardAsset("app.wasm");
    ASSERT_NE(wasm, nullptr);

    httplib::Result cached=get("/dashboard/app.wasm", {{"If-None-Match", wasm->etag}, {"Accept-Encoding", "gzip"}});
    ASSERT_TRUE(cached);
    EXPECT_EQ(cached->status, 304);
    EXPECT_TRUE(cached->body.empty());
    EXPECT_EQ(cached->get_header_value("ETag"), wasm->etag);

    httplib::Result stale=get("/dashboard/app.wasm", {{"If-None-Match", "\"0000\""}});
    ASSERT_TRUE(stale);
    EXPECT_EQ(stale->status, 200);
    EXPECT_EQ(stale->body.size(), wasm->size);
}

#else

TEST_F(DashboardServingTest, NotBuiltPageWithoutTheWebDashboard)
{
    // identity: httplib compresses this page itself for a client that takes br
    httplib::Result result=get("/dashboard", {{"Accept-Encoding", "identity"}});
    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 200);
    EXPECT_EQ(result->get_header_value("Content-Type"), "text/html; charset=utf-8");
    EXPECT_NE(result->body.find("Dashboard not built"), std::string::npos);

    httplib::Result asset=get("/dashboard/app.wasm");
    ASSERT_TRUE(asset);
    EXPECT_EQ(asset->status, 404);
}

#endif
