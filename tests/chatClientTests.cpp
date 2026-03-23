#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"
#include "arbiterAI/modelManager.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace arbiterAI
{

class ChatClientTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ModelManager::reset();
        
        // Add a test model
        ModelManager &mm = ModelManager::instance();
        ModelInfo model;
        model.model = "test-model";
        model.provider = "openai";
        mm.addModel(model);
    }
    
    void TearDown() override
    {
        ModelManager::reset();
    }
};

TEST_F(ChatClientTest, CreateChatClient)
{
    ArbiterAI ai;
    ChatConfig config;
    config.model = "test-model";
    config.maxTokens = 1000;
    config.temperature = 0.7;
    
    auto client = ai.createChatClient(config);
    
    // Should return nullptr since provider can't be initialized without API key
    // but the factory method itself should work
    // In a real scenario with mock providers, this would succeed
    EXPECT_EQ(client, nullptr);
}

TEST_F(ChatClientTest, ChatConfigDefaults)
{
    ChatConfig config;
    
    EXPECT_TRUE(config.model.empty());
    EXPECT_FALSE(config.maxTokens.has_value());
    EXPECT_FALSE(config.temperature.has_value());
    EXPECT_FALSE(config.systemPrompt.has_value());
    EXPECT_FALSE(config.enableCache);
}

TEST_F(ChatClientTest, ToolDefinitionConstruction)
{
    ToolDefinition tool;
    tool.name = "search";
    tool.description = "Search the web";
    
    ToolParameter param;
    param.name = "query";
    param.type = "string";
    param.description = "Search query";
    param.required = true;
    tool.parameters.push_back(param);
    
    EXPECT_EQ(tool.name, "search");
    EXPECT_EQ(tool.description, "Search the web");
    ASSERT_EQ(tool.parameters.size(), 1);
    EXPECT_EQ(tool.parameters[0].name, "query");
    EXPECT_TRUE(tool.parameters[0].required);
}

TEST_F(ChatClientTest, DownloadProgressDefaults)
{
    DownloadProgress progress;
    
    EXPECT_EQ(progress.status, DownloadStatus::NotApplicable);
    EXPECT_EQ(progress.bytesDownloaded, 0);
    EXPECT_EQ(progress.totalBytes, 0);
    EXPECT_FLOAT_EQ(progress.percentComplete, 0.0f);
    EXPECT_TRUE(progress.errorMessage.empty());
}

TEST_F(ChatClientTest, UsageStatsDefaults)
{
    UsageStats stats;
    
    EXPECT_EQ(stats.promptTokens, 0);
    EXPECT_EQ(stats.completionTokens, 0);
    EXPECT_EQ(stats.totalTokens, 0);
    EXPECT_DOUBLE_EQ(stats.estimatedCost, 0.0);
    EXPECT_EQ(stats.cachedResponses, 0);
    EXPECT_EQ(stats.completionCount, 0);
}

TEST_F(ChatClientTest, ToolCallConstruction)
{
    ToolCall toolCall;
    toolCall.id = "call_123";
    toolCall.name = "get_weather";
    toolCall.arguments = {{"location", "New York"}};
    
    EXPECT_EQ(toolCall.id, "call_123");
    EXPECT_EQ(toolCall.name, "get_weather");
    EXPECT_EQ(toolCall.arguments["location"], "New York");
}

TEST_F(ChatClientTest, CompletionResponseWithToolCalls)
{
    CompletionResponse response;
    response.text = "";
    
    ToolCall call1;
    call1.id = "call_1";
    call1.name = "search";
    call1.arguments = {{"query", "test"}};
    
    response.toolCalls.push_back(call1);
    
    ASSERT_EQ(response.toolCalls.size(), 1);
    EXPECT_EQ(response.toolCalls[0].name, "search");
}

TEST_F(ChatClientTest, CompletionRequestWithTools)
{
    CompletionRequest request;
    request.model = "test-model";
    request.messages = {{"user", "What's the weather?"}};
    
    ToolDefinition tool;
    tool.name = "get_weather";
    tool.description = "Get weather for a location";
    
    ToolParameter param;
    param.name = "location";
    param.type = "string";
    tool.parameters.push_back(param);
    
    std::vector<ToolDefinition> tools;
    tools.push_back(tool);
    request.tools = tools;
    
    EXPECT_EQ(request.model, "test-model");
    ASSERT_TRUE(request.tools.has_value());
    ASSERT_EQ(request.tools->size(), 1);
    EXPECT_EQ(request.tools->at(0).name, "get_weather");
}

} // namespace arbiterAI
