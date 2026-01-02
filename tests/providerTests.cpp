#include "arbiterAI/providers/anthropic.h"
#include "arbiterAI/providers/deepseek.h"
#include "arbiterAI/providers/openrouter.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <nlohmann/json.hpp>

namespace arbiterAI
{

class ProviderTest : public ::testing::Test
{
protected:
    void SetUp() override {}
    void TearDown() override {}

    // Anthropic helpers
    nlohmann::json createRequestBody(Anthropic& p, const CompletionRequest& r, bool s) { return p.createRequestBody(r, s); }
    ErrorCode parseResponse(Anthropic& p, const cpr::Response& r, CompletionResponse& resp) { return p.parseResponse(r, resp); }

    // Deepseek helpers
    nlohmann::json createRequestBody(Deepseek& p, const CompletionRequest& r, bool s) { return p.createRequestBody(r, s); }
    ErrorCode parseResponse(Deepseek& p, const cpr::Response& r, CompletionResponse& resp) { return p.parseResponse(r, resp); }

    // OpenRouter helpers
    nlohmann::json createRequestBody(OpenRouter_LLM& p, const CompletionRequest& r, bool s) { return p.createRequestBody(r, s); }
    ErrorCode parseResponse(OpenRouter_LLM& p, const cpr::Response& r, CompletionResponse& resp) { return p.parseResponse(r, resp); }
};

// --- ANTHROPIC TESTS ---

TEST_F(ProviderTest, Anthropic_CreateRequestBody)
{
    Anthropic provider;
    CompletionRequest request;
    request.model = "claude-3-opus-20240229";
    request.messages.push_back({"user", "Hello"});
    request.temperature = 0.5;
    request.max_tokens = 2048;

    nlohmann::json body = createRequestBody(provider, request, false);

    EXPECT_EQ(body["model"], "claude-3-opus-20240229");
    EXPECT_EQ(body["max_tokens"], 2048);
    EXPECT_EQ(body["temperature"], 0.5);
    EXPECT_EQ(body["stream"], false);
    
    ASSERT_TRUE(body.contains("messages"));
    ASSERT_TRUE(body["messages"].is_array());
    ASSERT_EQ(body["messages"].size(), 1);
    EXPECT_EQ(body["messages"][0]["role"], "user");
    EXPECT_EQ(body["messages"][0]["content"], "Hello");
}

TEST_F(ProviderTest, Anthropic_ParseResponse)
{
    Anthropic provider;
    
    std::string jsonStr = R"({
        "id": "msg_123",
        "type": "message",
        "role": "assistant",
        "content": [
            {
                "type": "text",
                "text": "Hello there!"
            }
        ],
        "model": "claude-3-opus-20240229",
        "stop_reason": "end_turn",
        "usage": {
            "input_tokens": 10,
            "output_tokens": 5
        }
    })";

    cpr::Response rawResponse;
    rawResponse.text = jsonStr;
    rawResponse.status_code = 200;

    CompletionResponse response;
    ErrorCode error = parseResponse(provider, rawResponse, response);

    EXPECT_EQ(error, ErrorCode::Success);
    EXPECT_EQ(response.text, "Hello there!");
    EXPECT_EQ(response.model, "claude-3-opus-20240229");
    EXPECT_EQ(response.provider, "anthropic");
    EXPECT_EQ(response.usage.prompt_tokens, 10);
    EXPECT_EQ(response.usage.completion_tokens, 5);
    EXPECT_EQ(response.usage.total_tokens, 15);
}

// --- DEEPSEEK TESTS ---

TEST_F(ProviderTest, DeepSeek_CreateRequestBody)
{
    Deepseek provider;
    CompletionRequest request;
    request.model = "deepseek-chat";
    request.messages.push_back({"user", "Hi"});
    
    nlohmann::json body = createRequestBody(provider, request, true);

    EXPECT_EQ(body["model"], "deepseek-chat");
    EXPECT_EQ(body["stream"], true);
    // DeepSeek is OpenAI compatible, so it should follow that structure
    ASSERT_TRUE(body["messages"].is_array());
    EXPECT_EQ(body["messages"][0]["content"], "Hi");
}

TEST_F(ProviderTest, DeepSeek_ParseResponse)
{
    Deepseek provider;
    // OpenAI compatible response format
    std::string jsonStr = R"({
        "id": "chatcmpl-123",
        "object": "chat.completion",
        "created": 1677652288,
        "model": "deepseek-chat",
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": "DeepSeek response"
            },
            "finish_reason": "stop"
        }],
        "usage": {
            "prompt_tokens": 9,
            "completion_tokens": 12,
            "total_tokens": 21
        }
    })";

    cpr::Response rawResponse;
    rawResponse.text = jsonStr;
    rawResponse.status_code = 200;

    CompletionResponse response;
    ErrorCode error = parseResponse(provider, rawResponse, response);

    EXPECT_EQ(error, ErrorCode::Success);
    EXPECT_EQ(response.text, "DeepSeek response");
    EXPECT_EQ(response.model, "deepseek-chat");
    EXPECT_EQ(response.provider, "deepseek");
}

// --- OPENROUTER TESTS ---

TEST_F(ProviderTest, OpenRouter_CreateRequestBody)
{
    OpenRouter_LLM provider;
    CompletionRequest request;
    request.model = "openai/gpt-3.5-turbo";
    request.messages.push_back({"user", "Test"});

    nlohmann::json body = createRequestBody(provider, request, false);
    
    EXPECT_EQ(body["model"], "openai/gpt-3.5-turbo");
    EXPECT_TRUE(body.contains("messages"));
}

} // namespace arbiterAI
