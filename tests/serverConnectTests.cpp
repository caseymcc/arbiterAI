#include <gtest/gtest.h>

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/providers/openai.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace arbiterAI
{

// Remote arbiterAI server endpoint
static const std::string SERVER_URL="http://192.168.2.114:8080";
static const std::string SERVER_API_URL=SERVER_URL+"/v1";

// Helper: check whether the remote server is reachable.
static bool isServerReachable()
{
    auto response=cpr::Get(
        cpr::Url{SERVER_URL+"/health"},
        cpr::Timeout{3000});

    if(response.error || response.status_code!=200)
        return false;

    try
    {
        auto j=nlohmann::json::parse(response.text);
        return j.value("status", "")=="ok";
    }
    catch(...)
    {
        return false;
    }
}

// Helper: query /v1/models on the remote server and return the first model id.
static std::string getFirstAvailableModel()
{
    auto response=cpr::Get(
        cpr::Url{SERVER_API_URL+"/models"},
        cpr::Timeout{5000});

    if(response.error || response.status_code!=200)
        return "";

    try
    {
        auto j=nlohmann::json::parse(response.text);
        if(j.contains("data") && j["data"].is_array() && !j["data"].empty())
        {
            return j["data"][0].value("id", "");
        }
    }
    catch(...)
    {
    }
    return "";
}

// Helper: find a model that is currently loaded and ready for inference
// on the remote server.  Returns empty string if none.
static std::string getLoadedModel()
{
    auto response=cpr::Get(
        cpr::Url{SERVER_URL+"/api/models/loaded"},
        cpr::Timeout{5000});

    if(response.error || response.status_code!=200)
        return "";

    try
    {
        auto j=nlohmann::json::parse(response.text);
        if(!j.contains("models") || !j["models"].is_array())
            return "";

        for(const auto &m : j["models"])
        {
            std::string state=m.value("state", "");
            if(state=="Loaded")
            {
                return m.value("model", "");
            }
        }
    }
    catch(...)
    {
    }
    return "";
}

class ServerConnectTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        if(!isServerReachable())
        {
            GTEST_SKIP()<<"arbiterAI server not reachable at "<<SERVER_URL;
        }
    }
};

// Fixture for tests that require a model loaded and ready on the server.
class ServerCompletionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        if(!isServerReachable())
        {
            GTEST_SKIP()<<"arbiterAI server not reachable at "<<SERVER_URL;
        }

        m_remoteModel=getLoadedModel();
        if(m_remoteModel.empty())
        {
            GTEST_SKIP()<<"No model currently loaded on remote server";
        }

        // Reset library state for a clean test
        ModelManager::reset();

        // Register the remote model in the local ModelManager so the library
        // can route requests through the OpenAI provider.
        ModelInfo info;
        info.model=m_remoteModel;
        info.provider="openai";
        info.contextWindow=131072;
        info.maxTokens=4096;
        ModelManager::instance().addModel(info);

        // Create an ArbiterAI instance and wire up the OpenAI provider
        // pointed at the remote arbiterAI server.
        m_ai=std::make_unique<ArbiterAI>();

        auto provider=std::make_unique<OpenAI>();
        provider->setApiUrl(SERVER_API_URL);
        provider->setApiKey("not-needed");
        provider->initialize(ModelManager::instance().getModels("openai"));
        m_ai->providers["openai"]=std::move(provider);
        m_ai->initialized=true;
    }

    void TearDown() override
    {
        m_ai.reset();
        ModelManager::reset();
    }

    std::string m_remoteModel;
    std::unique_ptr<ArbiterAI> m_ai;
};

// ── Health ────────────────────────────────────────────────────

TEST_F(ServerConnectTest, HealthEndpointReturnsOk)
{
    auto response=cpr::Get(
        cpr::Url{SERVER_URL+"/health"},
        cpr::Timeout{5000});

    ASSERT_EQ(response.status_code, 200);

    auto j=nlohmann::json::parse(response.text);
    EXPECT_EQ(j.value("status", ""), "ok");
    EXPECT_FALSE(j.value("version", "").empty());
}

// ── Model Listing ────────────────────────────────────────────

TEST_F(ServerConnectTest, ListModelsReturnsNonEmpty)
{
    auto response=cpr::Get(
        cpr::Url{SERVER_API_URL+"/models"},
        cpr::Timeout{5000});

    ASSERT_EQ(response.status_code, 200);

    auto j=nlohmann::json::parse(response.text);
    ASSERT_TRUE(j.contains("data"));
    ASSERT_TRUE(j["data"].is_array());
    EXPECT_GT(j["data"].size(), 0u);

    // Each entry should have an "id"
    for(const auto &model : j["data"])
    {
        EXPECT_TRUE(model.contains("id"));
        EXPECT_FALSE(model["id"].get<std::string>().empty());
    }
}

// ── Library Completion via OpenAI Provider ────────────────────

TEST_F(ServerCompletionTest, CompletionThroughLibrary)
{
    CompletionRequest request;
    request.model=m_remoteModel;
    request.messages={{"user", "Say hello in exactly one word."}};
    request.max_tokens=32;
    request.temperature=0.0;
    request.api_key="not-needed";

    CompletionResponse response;
    ErrorCode result=m_ai->completion(request, response);

    ASSERT_EQ(result, ErrorCode::Success)
        <<"Completion failed for model: "<<m_remoteModel;

    EXPECT_FALSE(response.text.empty())
        <<"Response text should not be empty";
    EXPECT_GT(response.usage.total_tokens, 0)
        <<"Token usage should be reported";
}

// ── Library Streaming Completion ──────────────────────────────

TEST_F(ServerCompletionTest, StreamingCompletionThroughLibrary)
{
    CompletionRequest request;
    request.model=m_remoteModel;
    request.messages={{"user", "Count from 1 to 3."}};
    request.max_tokens=64;
    request.temperature=0.0;
    request.api_key="not-needed";

    std::string accumulated;
    int chunkCount=0;

    auto callback=[&](const std::string &chunk)
    {
        accumulated+=chunk;
        chunkCount++;
    };

    ErrorCode result=m_ai->streamingCompletion(request, callback);

    ASSERT_EQ(result, ErrorCode::Success)
        <<"Streaming completion failed for model: "<<m_remoteModel;

    EXPECT_FALSE(accumulated.empty())
        <<"Accumulated streaming text should not be empty";
    EXPECT_GT(chunkCount, 0)
        <<"Should have received at least one chunk";
}

// ── ChatClient Session ────────────────────────────────────────

TEST_F(ServerCompletionTest, ChatClientSessionCompletion)
{
    ChatConfig config;
    config.model=m_remoteModel;
    config.maxTokens=64;
    config.temperature=0.0;
    config.apiKey="not-needed";

    auto client=m_ai->createChatClient(config);
    ASSERT_NE(client, nullptr)
        <<"Failed to create ChatClient for model: "<<m_remoteModel;

    CompletionRequest request;
    request.messages={{"user", "What is 2 + 2? Answer with just the number."}};
    request.max_tokens=16;
    request.temperature=0.0;

    CompletionResponse response;
    ErrorCode result=client->completion(request, response);

    ASSERT_EQ(result, ErrorCode::Success);
    EXPECT_FALSE(response.text.empty());

    // Verify the history was updated
    auto history=client->getHistory();
    EXPECT_GE(history.size(), 2u)
        <<"History should contain at least the user message and assistant response";

    // Verify usage stats
    UsageStats stats;
    client->getUsageStats(stats);
    EXPECT_GT(stats.totalTokens, 0);
    EXPECT_EQ(stats.completionCount, 1);
}

// ── Multi-turn Conversation ───────────────────────────────────

TEST_F(ServerCompletionTest, MultiTurnConversation)
{
    ChatConfig config;
    config.model=m_remoteModel;
    config.maxTokens=64;
    config.temperature=0.0;
    config.apiKey="not-needed";

    auto client=m_ai->createChatClient(config);
    ASSERT_NE(client, nullptr);

    // Turn 1
    CompletionRequest req1;
    req1.messages={{"user", "Remember the number 42."}};
    req1.max_tokens=64;
    req1.temperature=0.0;

    CompletionResponse resp1;
    ASSERT_EQ(client->completion(req1, resp1), ErrorCode::Success);
    EXPECT_FALSE(resp1.text.empty());

    // Turn 2 — references turn 1
    CompletionRequest req2;
    req2.messages={{"user", "What number did I ask you to remember?"}};
    req2.max_tokens=32;
    req2.temperature=0.0;

    CompletionResponse resp2;
    ASSERT_EQ(client->completion(req2, resp2), ErrorCode::Success);
    EXPECT_FALSE(resp2.text.empty());

    // History should contain system (if any) + 4 messages (2 user + 2 assistant)
    auto history=client->getHistory();
    EXPECT_GE(history.size(), 4u);

    // Usage should reflect both completions
    UsageStats stats;
    client->getUsageStats(stats);
    EXPECT_EQ(stats.completionCount, 2);
}

} // namespace arbiterAI
