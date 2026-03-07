/**
 * @file mockProviderTests.cpp
 * @brief Unit tests for the Mock provider
 */

#include "arbiterAI/providers/mock.h"
#include "arbiterAI/modelManager.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace arbiterAI
{

class MockProviderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        provider = std::make_unique<Mock>();
        
        // Create a minimal ModelInfo
        modelInfo.model = "mock-model";
        modelInfo.provider = "mock";
        modelInfo.ranking = 1;
    }

    void TearDown() override
    {
        provider.reset();
    }

    std::unique_ptr<Mock> provider;
    ModelInfo modelInfo;
};

// --- Basic Echo Tag Tests ---

TEST_F(MockProviderTest, BasicEchoTag)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "Test <echo>Expected Response</echo>"}};

    CompletionResponse response;
    ErrorCode result = provider->completion(request, modelInfo, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_EQ(response.text, "Expected Response");
    EXPECT_EQ(response.model, "mock-model");
}

TEST_F(MockProviderTest, EchoTagWithWhitespace)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "Test <echo>  \n  Trimmed  \n  </echo>"}};

    CompletionResponse response;
    ErrorCode result = provider->completion(request, modelInfo, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_EQ(response.text, "Trimmed");
}

TEST_F(MockProviderTest, MultilineEchoTag)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", R"(Generate code <echo>
def hello():
    print("Hello, World!")
    return True
</echo>)"}};

    CompletionResponse response;
    ErrorCode result = provider->completion(request, modelInfo, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_TRUE(response.text.find("def hello()") != std::string::npos);
    EXPECT_TRUE(response.text.find("print(\"Hello, World!\")") != std::string::npos);
}

TEST_F(MockProviderTest, CaseInsensitiveTag)
{
    // Test uppercase
    {
        CompletionRequest request;
        request.model = "mock-model";
        request.messages = {{"user", "Test <ECHO>Uppercase</ECHO>"}};

        CompletionResponse response;
        provider->completion(request, modelInfo, response);
        EXPECT_EQ(response.text, "Uppercase");
    }

    // Test mixed case
    {
        CompletionRequest request;
        request.model = "mock-model";
        request.messages = {{"user", "Test <Echo>MixedCase</Echo>"}};

        CompletionResponse response;
        provider->completion(request, modelInfo, response);
        EXPECT_EQ(response.text, "MixedCase");
    }
}

TEST_F(MockProviderTest, FirstEchoTagUsed)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "Test <echo>First</echo> and <echo>Second</echo>"}};

    CompletionResponse response;
    ErrorCode result = provider->completion(request, modelInfo, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_EQ(response.text, "First");
}

TEST_F(MockProviderTest, EchoTagAcrossMultipleMessages)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {
        {"system", "You are a helpful assistant"},
        {"user", "First message"},
        {"assistant", "Response"},
        {"user", "Second message <echo>Expected</echo>"}
    };

    CompletionResponse response;
    ErrorCode result = provider->completion(request, modelInfo, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_EQ(response.text, "Expected");
}

TEST_F(MockProviderTest, NoEchoTagDefaultResponse)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "No echo tag in this message"}};

    CompletionResponse response;
    ErrorCode result = provider->completion(request, modelInfo, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_TRUE(response.text.find("mock response") != std::string::npos);
    EXPECT_TRUE(response.text.find("<echo>") != std::string::npos);
}

TEST_F(MockProviderTest, EmptyEchoTag)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "Empty tag <echo></echo>"}};

    CompletionResponse response;
    ErrorCode result = provider->completion(request, modelInfo, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_EQ(response.text, "");
}

// --- Token Usage Tests ---

TEST_F(MockProviderTest, TokenUsageCalculation)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "Short message <echo>Short response</echo>"}};

    CompletionResponse response;
    provider->completion(request, modelInfo, response);

    EXPECT_GT(response.usage.prompt_tokens, 0);
    EXPECT_GT(response.usage.completion_tokens, 0);
    EXPECT_EQ(response.usage.total_tokens, 
              response.usage.prompt_tokens + response.usage.completion_tokens);
}

TEST_F(MockProviderTest, TokenUsageLongerText)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", R"(
        This is a much longer message that should generate more tokens.
        It has multiple sentences and spans several lines.
        <echo>
        This is also a longer response with multiple lines.
        It should result in higher token counts.
        </echo>
    )"}};

    CompletionResponse response1;
    provider->completion(request, modelInfo, response1);

    // Compare with shorter request
    CompletionRequest shortRequest;
    shortRequest.model = "mock-model";
    shortRequest.messages = {{"user", "Hi <echo>Hi</echo>"}};

    CompletionResponse response2;
    provider->completion(shortRequest, modelInfo, response2);

    // Longer text should have more tokens
    EXPECT_GT(response1.usage.total_tokens, response2.usage.total_tokens);
}

// --- Streaming Tests ---

TEST_F(MockProviderTest, StreamingCompletion)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "Stream this <echo>Hello streaming world!</echo>"}};

    std::string accumulated;
    int chunkCount = 0;

    auto callback = [&](const std::string& chunk)
    {
        if (!chunk.empty())
        {
            accumulated += chunk;
            chunkCount++;
        }
    };

    ErrorCode result = provider->streamingCompletion(request, callback);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_EQ(accumulated, "Hello streaming world!");
    EXPECT_GT(chunkCount, 1); // Should be split into multiple chunks
}

TEST_F(MockProviderTest, StreamingMultipleChunks)
{
    CompletionRequest request;
    request.model = "mock-model";
    // Create a longer response to ensure multiple chunks
    std::string longResponse(100, 'A'); // 100 'A' characters
    request.messages = {{"user", "Test <echo>" + longResponse + "</echo>"}};

    std::vector<std::string> chunks;
    auto callback = [&](const std::string& chunk)
    {
        if (!chunk.empty())
        {
            chunks.push_back(chunk);
        }
    };

    provider->streamingCompletion(request, callback);

    // Should have multiple chunks
    EXPECT_GT(chunks.size(), 1);

    // Reconstruct full response
    std::string full;
    for (const auto& chunk : chunks)
    {
        full += chunk;
    }
    EXPECT_EQ(full, longResponse);
}

TEST_F(MockProviderTest, StreamingNoEchoTag)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "No echo tag"}};

    std::string accumulated;
    auto callback = [&](const std::string& chunk)
    {
        if (!chunk.empty())
        {
            accumulated += chunk;
        }
    };

    ErrorCode result = provider->streamingCompletion(request, callback);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_TRUE(accumulated.find("mock response") != std::string::npos);
}

// --- Model and Provider Tests ---

TEST_F(MockProviderTest, GetAvailableModels)
{
    std::vector<std::string> models;
    ErrorCode result = provider->getAvailableModels(models);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_EQ(models.size(), 1);
    EXPECT_EQ(models[0], "mock-model");
}

TEST_F(MockProviderTest, GetEmbeddingsNotImplemented)
{
    EmbeddingRequest request;
    request.model = "mock-model";
    request.input = {"test"};

    EmbeddingResponse response;
    ErrorCode result = provider->getEmbeddings(request, response);

    EXPECT_EQ(result, ErrorCode::NotImplemented);
}

// --- Response Metadata Tests ---

TEST_F(MockProviderTest, ResponseNotFromCache)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "Test <echo>Response</echo>"}};

    CompletionResponse response;
    provider->completion(request, modelInfo, response);

    EXPECT_FALSE(response.fromCache);
}

TEST_F(MockProviderTest, ResponseModelSet)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "Test <echo>Response</echo>"}};

    CompletionResponse response;
    provider->completion(request, modelInfo, response);

    EXPECT_EQ(response.model, "mock-model");
}

// --- Edge Cases ---

TEST_F(MockProviderTest, EmptyMessages)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {};

    CompletionResponse response;
    ErrorCode result = provider->completion(request, modelInfo, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_TRUE(response.text.find("mock response") != std::string::npos);
}

TEST_F(MockProviderTest, EmptyMessageContent)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", ""}};

    CompletionResponse response;
    ErrorCode result = provider->completion(request, modelInfo, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_TRUE(response.text.find("mock response") != std::string::npos);
}

TEST_F(MockProviderTest, NestedTags)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", "Outer <echo>Inner <echo>nested</echo> text</echo>"}};

    CompletionResponse response;
    provider->completion(request, modelInfo, response);

    // Should extract first match (greedy but non-greedy regex)
    EXPECT_TRUE(!response.text.empty());
}

TEST_F(MockProviderTest, SpecialCharactersInEcho)
{
    CompletionRequest request;
    request.model = "mock-model";
    request.messages = {{"user", R"(Test <echo>Special chars: !@#$%^&*(){}[]|\"'</echo>)"}};

    CompletionResponse response;
    provider->completion(request, modelInfo, response);

    EXPECT_EQ(response.text, R"(Special chars: !@#$%^&*(){}[]|\"')");
}

} // namespace arbiterAI
