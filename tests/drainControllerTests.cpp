#include <gtest/gtest.h>

#include "drainController.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace arbiterAI
{
namespace server
{

namespace
{

/// An httplib server on a free localhost port, wired to a DrainController the
/// way registerRoutes() wires the real one.
class DrainTestServer {
public:
    explicit DrainTestServer(DrainController &drainController)
    {
        drainController.attach(m_server);
        m_server.set_pre_routing_handler([&drainController](const httplib::Request &req, httplib::Response &res)
        {
            return drainController.gate(req, res);
        });
    }

    ~DrainTestServer()
    {
        m_server.stop();
        if(m_thread.joinable())
            m_thread.join();
    }

    httplib::Server &server() { return m_server; }

    void start()
    {
        m_port=m_server.bind_to_any_port("127.0.0.1");
        m_thread=std::thread([this]() { m_server.listen_after_bind(); });
        m_server.wait_until_ready();
    }

    std::unique_ptr<httplib::Client> client() const
    {
        std::unique_ptr<httplib::Client> cli=std::make_unique<httplib::Client>("127.0.0.1", m_port);
        cli->set_read_timeout(10, 0);
        return cli;
    }

private:
    httplib::Server m_server;
    std::thread m_thread;
    int m_port=0;
};

bool waitFor(const std::function<bool()> &condition, std::chrono::milliseconds timeout=std::chrono::milliseconds(5000))
{
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+timeout;
    while(std::chrono::steady_clock::now()<deadline)
    {
        if(condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return condition();
}

long long elapsedMs(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
}

} // anonymous namespace

TEST(DrainControllerTest, GateAnswers503WhileDraining)
{
    DrainController drainController;
    drainController.setRetryAfterSeconds(7);
    DrainTestServer testServer(drainController);
    testServer.server().Get("/ping", [](const httplib::Request &, httplib::Response &res)
    {
        res.set_content("pong", "text/plain");
    });
    testServer.start();

    httplib::Result before=testServer.client()->Get("/ping");
    ASSERT_TRUE(before);
    EXPECT_EQ(before->status, 200);

    drainController.beginDrain();

    httplib::Result during=testServer.client()->Get("/ping");
    ASSERT_TRUE(during);
    EXPECT_EQ(during->status, 503);
    EXPECT_EQ(during->get_header_value("Retry-After"), "7");
    nlohmann::json body=nlohmann::json::parse(during->body, nullptr, false);
    ASSERT_TRUE(body.is_object());
    EXPECT_EQ(body["error"]["code"], "server_shutting_down");
    EXPECT_EQ(body["error"]["type"], "server_error");

    // The first request's count ends in httplib's logger, which may still be running.
    EXPECT_TRUE(waitFor([&drainController]() { return drainController.inFlightRequests()==0; }));
}

TEST(DrainControllerTest, CountsRequestWhileHandlerRuns)
{
    DrainController drainController;
    DrainTestServer testServer(drainController);
    std::promise<void> entered;
    std::promise<void> release;
    std::shared_future<void> released=release.get_future().share();
    testServer.server().Get("/slow", [&entered, released](const httplib::Request &, httplib::Response &res)
    {
        entered.set_value();
        released.wait();
        res.set_content("done", "text/plain");
    });
    testServer.start();

    std::future<int> status=std::async(std::launch::async, [&testServer]()
    {
        httplib::Result result=testServer.client()->Get("/slow");
        return result?result->status:-1;
    });

    entered.get_future().wait();
    EXPECT_EQ(drainController.inFlightRequests(), 1u);
    EXPECT_FALSE(drainController.isIdle());

    release.set_value();
    EXPECT_EQ(status.get(), 200);
    EXPECT_TRUE(waitFor([&drainController]() { return drainController.inFlightRequests()==0; }));
}

TEST(DrainControllerTest, StreamedResponseCountedUntilWritten)
{
    DrainController drainController;
    DrainTestServer testServer(drainController);
    std::atomic<bool> handlerReturned{false};
    std::promise<void> firstChunkSent;
    std::promise<void> finishStream;
    std::shared_future<void> finish=finishStream.get_future().share();
    testServer.server().Get("/stream", [&handlerReturned, &firstChunkSent, finish](const httplib::Request &, httplib::Response &res)
    {
        res.set_chunked_content_provider("text/event-stream", [&firstChunkSent, finish](size_t, httplib::DataSink &sink)
        {
            std::string first="data: one\n\n";
            sink.write(first.data(), first.size());
            firstChunkSent.set_value();
            finish.wait();
            std::string last="data: two\n\n";
            sink.write(last.data(), last.size());
            sink.done();
            return true;
        });
        handlerReturned=true;
    });
    testServer.start();

    std::future<std::string> body=std::async(std::launch::async, [&testServer]()
    {
        httplib::Result result=testServer.client()->Get("/stream");
        return result?result->body:std::string();
    });

    firstChunkSent.get_future().wait();
    EXPECT_TRUE(handlerReturned.load());
    EXPECT_EQ(drainController.inFlightRequests(), 1u);

    finishStream.set_value();
    EXPECT_EQ(body.get(), "data: one\n\ndata: two\n\n");
    EXPECT_TRUE(waitFor([&drainController]() { return drainController.inFlightRequests()==0; }));
}

TEST(DrainControllerTest, DrainWaitsForStreamToFinish)
{
    DrainController drainController;
    DrainTestServer testServer(drainController);
    std::promise<void> firstChunkSent;
    testServer.server().Get("/stream", [&firstChunkSent](const httplib::Request &, httplib::Response &res)
    {
        res.set_chunked_content_provider("text/event-stream", [&firstChunkSent](size_t, httplib::DataSink &sink)
        {
            std::string first="data: one\n\n";
            sink.write(first.data(), first.size());
            firstChunkSent.set_value();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            sink.done();
            return true;
        });
    });
    testServer.start();

    std::future<int> status=std::async(std::launch::async, [&testServer]()
    {
        httplib::Result result=testServer.client()->Get("/stream");
        return result?result->status:-1;
    });

    firstChunkSent.get_future().wait();
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    EXPECT_TRUE(drainController.drain(std::chrono::seconds(10)));
    long long waitedMs=elapsedMs(start);

    EXPECT_GE(waitedMs, 200);
    EXPECT_LT(waitedMs, 5000);
    EXPECT_EQ(status.get(), 200);
    EXPECT_EQ(drainController.inFlightRequests(), 0u);
}

TEST(DrainControllerTest, DrainWaitsForBusyProbe)
{
    DrainController drainController;
    std::atomic<size_t> jobs{1};
    drainController.addBusyProbe("jobs", [&jobs]() { return jobs.load(); });

    std::thread finisher([&jobs]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        jobs=0;
    });

    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    bool finished=drainController.drain(std::chrono::seconds(10));
    long long waitedMs=elapsedMs(start);
    finisher.join();

    EXPECT_TRUE(finished);
    EXPECT_GE(waitedMs, 250);
    EXPECT_LT(waitedMs, 5000);
}

TEST(DrainControllerTest, DrainStopsAtTimeout)
{
    DrainController drainController;
    drainController.addBusyProbe("downloads", []() { return static_cast<size_t>(1); });

    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    bool finished=drainController.drain(std::chrono::milliseconds(300));
    long long waitedMs=elapsedMs(start);

    EXPECT_FALSE(finished);
    EXPECT_TRUE(drainController.isDraining());
    EXPECT_GE(waitedMs, 300);
    EXPECT_LT(waitedMs, 3000);
}

} // namespace server
} // namespace arbiterAI
