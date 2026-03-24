#include <gtest/gtest.h>

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/telemetryCollector.h"
#include "arbiterAI/modelManager.h"

#include <filesystem>
#include <string>

namespace arbiterAI
{

static const std::string MODEL_NAME="qwen2.5-7b-instruct";
static const std::string SMALL_MODEL_NAME="qwen2.5-1.5b-instruct";

class LlamaProviderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ModelRuntime::reset();
        TelemetryCollector::reset();

        ArbiterAI &ai=ArbiterAI::instance();
        ai.initialize({"tests/config"});

        // Verify the llama model is known to ModelManager
        std::optional<ModelInfo> info=ModelManager::instance().getModelInfo(MODEL_NAME);
        if(!info||info->provider!="llama")
        {
            GTEST_SKIP() << "Model '" << MODEL_NAME << "' not found in config or not a llama model";
        }

        // Check that the model has downloadable variants
        if(info->variants.empty())
        {
            GTEST_SKIP() << "Model '" << MODEL_NAME << "' has no variants configured";
        }
    }

    void TearDown() override
    {
        ModelRuntime::reset();
        TelemetryCollector::reset();
    }
};

TEST_F(LlamaProviderTest, BasicCompletion)
{
    ChatConfig config;
    config.model=MODEL_NAME;
    config.maxTokens=64;

    std::shared_ptr<ChatClient> client=ArbiterAI::instance().createChatClient(config);
    ASSERT_NE(client, nullptr);

    CompletionRequest request;
    request.model=MODEL_NAME;
    request.max_tokens=64;
    request.messages={{"user", "What is 2+2? Answer with just the number."}};

    CompletionResponse response;
    ErrorCode result=client->completion(request, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_FALSE(response.text.empty());
    EXPECT_EQ(response.provider, "llama");
    EXPECT_EQ(response.model, MODEL_NAME);
}

TEST_F(LlamaProviderTest, StreamingCompletion)
{
    ChatConfig config;
    config.model=MODEL_NAME;
    config.maxTokens=64;

    std::shared_ptr<ChatClient> client=ArbiterAI::instance().createChatClient(config);
    ASSERT_NE(client, nullptr);

    CompletionRequest request;
    request.model=MODEL_NAME;
    request.max_tokens=64;
    request.messages={{"user", "Say hello in one sentence."}};

    std::string accumulated;
    int chunkCount=0;

    auto callback=[&](const std::string &chunk, bool done)
    {
        if(!done)
        {
            accumulated+=chunk;
            chunkCount++;
        }
    };

    ErrorCode result=client->streamingCompletion(request, callback);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_FALSE(accumulated.empty());
    EXPECT_GT(chunkCount, 0);
}

TEST_F(LlamaProviderTest, TokenUsageReported)
{
    ChatConfig config;
    config.model=MODEL_NAME;
    config.maxTokens=32;

    std::shared_ptr<ChatClient> client=ArbiterAI::instance().createChatClient(config);
    ASSERT_NE(client, nullptr);

    CompletionRequest request;
    request.model=MODEL_NAME;
    request.max_tokens=32;
    request.messages={{"user", "Hi"}};

    CompletionResponse response;
    ErrorCode result=client->completion(request, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_GT(response.usage.prompt_tokens, 0);
    EXPECT_GT(response.usage.completion_tokens, 0);
    EXPECT_GT(response.usage.total_tokens, 0);
}

TEST_F(LlamaProviderTest, ModelRuntimeTracksState)
{
    // Load the model via ModelRuntime
    ErrorCode loadResult=ModelRuntime::instance().loadModel(MODEL_NAME, "Q4_K_M", 4096);
    EXPECT_EQ(loadResult, ErrorCode::Success);

    std::optional<LoadedModel> state=ModelRuntime::instance().getModelState(MODEL_NAME);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Loaded);
    EXPECT_EQ(state->variant, "Q4_K_M");
    EXPECT_NE(state->llamaModel, nullptr);
    EXPECT_NE(state->llamaCtx, nullptr);

    // Unload
    ErrorCode unloadResult=ModelRuntime::instance().unloadModel(MODEL_NAME);
    EXPECT_EQ(unloadResult, ErrorCode::Success);

    state=ModelRuntime::instance().getModelState(MODEL_NAME);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Unloaded);
    EXPECT_EQ(state->llamaModel, nullptr);
    EXPECT_EQ(state->llamaCtx, nullptr);
}

TEST_F(LlamaProviderTest, TelemetryRecorded)
{
    ChatConfig config;
    config.model=MODEL_NAME;
    config.maxTokens=16;

    std::shared_ptr<ChatClient> client=ArbiterAI::instance().createChatClient(config);
    ASSERT_NE(client, nullptr);

    CompletionRequest request;
    request.model=MODEL_NAME;
    request.max_tokens=16;
    request.messages={{"user", "Hello"}};

    CompletionResponse response;
    client->completion(request, response);

    std::vector<InferenceStats> history=TelemetryCollector::instance().getHistory(std::chrono::minutes(1));
    EXPECT_GE(history.size(), 1);
    if(!history.empty())
    {
        EXPECT_EQ(history.back().model, MODEL_NAME);
        EXPECT_GT(history.back().completionTokens, 0);
        EXPECT_GT(history.back().tokensPerSecond, 0.0);
    }
}

TEST_F(LlamaProviderTest, SystemPromptApplied)
{
    ChatConfig config;
    config.model=MODEL_NAME;
    config.maxTokens=64;
    config.systemPrompt="You are a calculator. Only output numbers.";

    std::shared_ptr<ChatClient> client=ArbiterAI::instance().createChatClient(config);
    ASSERT_NE(client, nullptr);

    CompletionRequest request;
    request.model=MODEL_NAME;
    request.max_tokens=64;
    request.messages={
        {"system", "You are a calculator. Only output numbers."},
        {"user", "What is 5+3?"}
    };

    CompletionResponse response;
    ErrorCode result=client->completion(request, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_FALSE(response.text.empty());
}

TEST_F(LlamaProviderTest, ModelSwitching)
{
    // Verify the small model is also configured
    std::optional<ModelInfo> smallInfo=ModelManager::instance().getModelInfo(SMALL_MODEL_NAME);
    if(!smallInfo||smallInfo->provider!="llama"||smallInfo->variants.empty())
    {
        GTEST_SKIP() << "Model '" << SMALL_MODEL_NAME << "' not found in config or has no variants";
    }

    // Create a client with the 7B model
    ChatConfig config;
    config.model=MODEL_NAME;
    config.maxTokens=32;

    std::shared_ptr<ChatClient> client=ArbiterAI::instance().createChatClient(config);
    ASSERT_NE(client, nullptr);
    EXPECT_EQ(client->getModel(), MODEL_NAME);

    // Run a prompt on the 7B model
    CompletionRequest request1;
    request1.model=MODEL_NAME;
    request1.max_tokens=32;
    request1.messages={{"user", "What is 2+2? Answer with just the number."}};

    CompletionResponse response1;
    ErrorCode result1=client->completion(request1, response1);

    EXPECT_EQ(result1, ErrorCode::Success);
    EXPECT_FALSE(response1.text.empty());
    EXPECT_EQ(response1.provider, "llama");
    EXPECT_EQ(response1.model, MODEL_NAME);

    // Switch to the 1.5B model
    ErrorCode switchResult=client->switchModel(SMALL_MODEL_NAME);
    EXPECT_EQ(switchResult, ErrorCode::Success);
    EXPECT_EQ(client->getModel(), SMALL_MODEL_NAME);

    // Run a prompt on the 1.5B model
    CompletionRequest request2;
    request2.model=SMALL_MODEL_NAME;
    request2.max_tokens=32;
    request2.messages={{"user", "What is 3+5? Answer with just the number."}};

    CompletionResponse response2;
    ErrorCode result2=client->completion(request2, response2);

    EXPECT_EQ(result2, ErrorCode::Success);
    EXPECT_FALSE(response2.text.empty());
    EXPECT_EQ(response2.provider, "llama");
    EXPECT_EQ(response2.model, SMALL_MODEL_NAME);

    // Verify both models produced different responses from different models
    EXPECT_NE(response1.model, response2.model);
}

} // namespace arbiterAI
