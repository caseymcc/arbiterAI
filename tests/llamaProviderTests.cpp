#include <gtest/gtest.h>

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/telemetryCollector.h"
#include "arbiterAI/modelManager.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>

namespace arbiterAI
{

static const std::string MODEL_NAME="Qwen2.5-7B-Instruct";
static const std::string SMALL_MODEL_NAME="Qwen2.5-1.5B-Instruct";
static const std::string MODEL_FILE="Qwen2.5-7B-Instruct-Q4_K_M.gguf";
static const std::string SMALL_MODEL_FILE="Qwen2.5-1.5B-Instruct-Q4_K_M.gguf";

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

        // Check that the GGUF file actually exists on disk
        std::string filePath="/models/"+MODEL_FILE;
        if(!std::filesystem::exists(filePath))
        {
            GTEST_SKIP() << "Model file not found at " << filePath;
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

    // Verify the small model file exists
    std::string smallFilePath="/models/"+SMALL_MODEL_FILE;
    if(!std::filesystem::exists(smallFilePath))
    {
        GTEST_SKIP() << "Small model file not found at " << smallFilePath;
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

// ─── Config injection tests ──────────────────────────────────────────────

static const std::string INJECTED_MODEL_NAME="injected-qwen-test";

class LlamaConfigInjectionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ModelRuntime::reset();
        TelemetryCollector::reset();

        ArbiterAI &ai=ArbiterAI::instance();
        ai.initialize({"tests/config"});

        // Use the small model file for injection tests (faster to load)
        std::string filePath="/models/"+SMALL_MODEL_FILE;
        if(!std::filesystem::exists(filePath))
        {
            GTEST_SKIP() << "Small model file not found at " << filePath;
        }
    }

    void TearDown() override
    {
        ModelRuntime::instance().unloadModel(INJECTED_MODEL_NAME);
        ModelRuntime::reset();
        TelemetryCollector::reset();
    }

    nlohmann::json buildInjectedModelJson() const
    {
        return nlohmann::json{
            {"model", INJECTED_MODEL_NAME},
            {"provider", "llama"},
            {"ranking", 1},
            {"version", "1.1.0"},
            {"context_window", 4096},
            {"max_tokens", 2048},
            {"max_output_tokens", 1024},
            {"hardware_requirements", {
                {"min_system_ram_mb", 2048},
                {"parameter_count", "1.5B"}
            }},
            {"context_scaling", {
                {"base_context", 4096},
                {"max_context", 32768},
                {"vram_per_1k_context_mb", 32}
            }},
            {"variants", nlohmann::json::array({
                {
                    {"quantization", "Q4_K_M"},
                    {"file_size_mb", 941},
                    {"min_vram_mb", 1536},
                    {"recommended_vram_mb", 2048},
                    {"download", {
                        {"url", ""},
                        {"sha256", ""},
                        {"filename", SMALL_MODEL_FILE}
                    }}
                }
            })}
        };
    }
};

TEST_F(LlamaConfigInjectionTest, InjectAndLoad)
{
    nlohmann::json modelJson=buildInjectedModelJson();

    std::string error;
    bool added=ModelManager::instance().addModelFromJson(modelJson, error);
    ASSERT_TRUE(added) << "addModelFromJson failed: " << error;

    // Verify it was registered
    std::optional<ModelInfo> info=ModelManager::instance().getModelInfo(INJECTED_MODEL_NAME);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->provider, "llama");
    EXPECT_EQ(info->variants.size(), 1u);
    EXPECT_EQ(info->variants[0].quantization, "Q4_K_M");
    EXPECT_EQ(info->variants[0].download.filename, SMALL_MODEL_FILE);

    // Load the injected model via ModelRuntime
    ErrorCode loadResult=ModelRuntime::instance().loadModel(INJECTED_MODEL_NAME, "Q4_K_M", 4096);
    EXPECT_EQ(loadResult, ErrorCode::Success);

    // Verify model state
    std::optional<LoadedModel> state=ModelRuntime::instance().getModelState(INJECTED_MODEL_NAME);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Loaded);
    EXPECT_EQ(state->variant, "Q4_K_M");
    EXPECT_NE(state->llamaModel, nullptr);
    EXPECT_NE(state->llamaCtx, nullptr);
}

TEST_F(LlamaConfigInjectionTest, InjectAndRunCompletion)
{
    nlohmann::json modelJson=buildInjectedModelJson();

    std::string error;
    bool added=ModelManager::instance().addModelFromJson(modelJson, error);
    ASSERT_TRUE(added) << "addModelFromJson failed: " << error;

    // Create a ChatClient with the injected model
    ChatConfig config;
    config.model=INJECTED_MODEL_NAME;
    config.maxTokens=32;

    std::shared_ptr<ChatClient> client=ArbiterAI::instance().createChatClient(config);
    ASSERT_NE(client, nullptr) << "Failed to create ChatClient for injected model";
    EXPECT_EQ(client->getModel(), INJECTED_MODEL_NAME);

    CompletionRequest request;
    request.model=INJECTED_MODEL_NAME;
    request.max_tokens=32;
    request.messages={{"user", "What is 1+1? Answer with just the number."}};

    CompletionResponse response;
    ErrorCode result=client->completion(request, response);

    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_FALSE(response.text.empty());
    EXPECT_EQ(response.provider, "llama");
    EXPECT_EQ(response.model, INJECTED_MODEL_NAME);
    EXPECT_GT(response.usage.total_tokens, 0);
}

TEST_F(LlamaConfigInjectionTest, InjectDuplicateFails)
{
    nlohmann::json modelJson=buildInjectedModelJson();

    std::string error;
    bool added=ModelManager::instance().addModelFromJson(modelJson, error);
    ASSERT_TRUE(added) << "First injection failed: " << error;

    // Try to inject same model name again
    bool addedAgain=ModelManager::instance().addModelFromJson(modelJson, error);
    EXPECT_FALSE(addedAgain);
    EXPECT_NE(error.find("already exists"), std::string::npos);
}

TEST_F(LlamaConfigInjectionTest, InjectWithoutVariantsFails)
{
    nlohmann::json modelJson={
        {"model", "no-variants-llama"},
        {"provider", "llama"},
        {"ranking", 1},
        {"version", "1.1.0"}
    };

    std::string error;
    bool added=ModelManager::instance().addModelFromJson(modelJson, error);
    ASSERT_TRUE(added) << "addModelFromJson failed: " << error;

    // Loading a llama model without variants should fail
    ErrorCode loadResult=ModelRuntime::instance().loadModel("no-variants-llama");
    EXPECT_EQ(loadResult, ErrorCode::InvalidRequest);
}

} // namespace arbiterAI
