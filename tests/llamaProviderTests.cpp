#include <gtest/gtest.h>

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/providers/llama.h"
#include "arbiterAI/telemetryCollector.h"
#include "arbiterAI/modelManager.h"

#include <nlohmann/json.hpp>
#include <algorithm>
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

// ── cache_prompt prefix reuse ────────────────────────────────────────────

TEST(KvPrefixReuse, EmptyCacheReusesNothing)
{
    EXPECT_EQ(kvPrefixReuseLength({}, {1, 2, 3}), 0);
}

TEST(KvPrefixReuse, GrowingPromptReusesWholeCache)
{
    // Typical agent turn: previous prompt + generated reply + new tool result
    EXPECT_EQ(kvPrefixReuseLength({1, 2, 3, 4}, {1, 2, 3, 4, 5, 6, 7}), 4);
}

TEST(KvPrefixReuse, IdenticalPromptLeavesOneTokenToDecode)
{
    // The last position must be re-decoded so sampling has fresh logits
    EXPECT_EQ(kvPrefixReuseLength({1, 2, 3, 4}, {1, 2, 3, 4}), 3);
}

TEST(KvPrefixReuse, DivergenceTruncatesAtMismatch)
{
    EXPECT_EQ(kvPrefixReuseLength({1, 2, 3, 4}, {1, 2, 9, 4, 5}), 2);
}

TEST(KvPrefixReuse, ShorterPromptCapsBelowPromptLength)
{
    EXPECT_EQ(kvPrefixReuseLength({1, 2, 3, 4, 5, 6}, {1, 2, 3}), 2);
}

TEST(KvPrefixReuse, SingleTokenPromptNeverReuses)
{
    EXPECT_EQ(kvPrefixReuseLength({1, 2, 3}, {1}), 0);
}

TEST(KvPrefixReuse, CompletelyDifferentPromptReusesNothing)
{
    EXPECT_EQ(kvPrefixReuseLength({7, 8, 9}, {1, 2, 3}), 0);
}

TEST(KvPrefixReuse, KvCacheTokensRequiresLoadedModel)
{
    ModelRuntime::reset();
    EXPECT_EQ(ModelRuntime::instance().kvCacheTokens("not-loaded"), nullptr);
}

// ─── Vision-language model tests ─────────────────────────────────────────
//
// These exercise the libmtmd path end to end (projector load → mtmd_tokenize →
// chunked prefill → generation).  They need both GGUF files staged in /models;
// fetch them with:
//   huggingface-cli download Qwen/Qwen3-VL-2B-Instruct-GGUF \
//       Qwen3VL-2B-Instruct-Q4_K_M.gguf mmproj-Qwen3VL-2B-Instruct-Q8_0.gguf \
//       --local-dir model_cache

static const std::string VISION_MODEL_NAME="injected-qwen3vl-test";
static const std::string VISION_MODEL_FILE="Qwen3VL-2B-Instruct-Q4_K_M.gguf";
static const std::string VISION_MMPROJ_FILE="mmproj-Qwen3VL-2B-Instruct-Q8_0.gguf";

class LlamaVisionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ModelRuntime::reset();
        TelemetryCollector::reset();

        ArbiterAI::instance().initialize({"tests/config"});

        if(!std::filesystem::exists("/models/"+VISION_MODEL_FILE))
        {
            GTEST_SKIP() << "Vision model file not found at /models/" << VISION_MODEL_FILE;
        }
        if(!std::filesystem::exists("/models/"+VISION_MMPROJ_FILE))
        {
            GTEST_SKIP() << "Projector file not found at /models/" << VISION_MMPROJ_FILE;
        }

        std::string error;
        ASSERT_TRUE(ModelManager::instance().addModelFromJson(buildVisionModelJson(), error))<<error;
    }

    void TearDown() override
    {
        ModelRuntime::instance().unloadModel(VISION_MODEL_NAME);
        ModelRuntime::reset();
        TelemetryCollector::reset();
    }

    nlohmann::json buildVisionModelJson() const
    {
        return nlohmann::json{
            {"model", VISION_MODEL_NAME},
            {"provider", "llama"},
            {"ranking", 1},
            {"version", "1.1.0"},
            {"input_modalities", nlohmann::json::array({"text", "image"})},
            {"context_window", 8192},
            {"max_tokens", 2048},
            {"max_output_tokens", 256},
            {"hardware_requirements", {
                {"min_system_ram_mb", 4096},
                {"parameter_count", "2B"}
            }},
            {"context_scaling", {
                {"base_context", 4096},
                {"max_context", 32768},
                {"vram_per_1k_context_mb", 32}
            }},
            {"variants", nlohmann::json::array({
                {
                    {"quantization", "Q4_K_M"},
                    {"file_size_mb", 1056},
                    {"min_vram_mb", 2048},
                    {"recommended_vram_mb", 3072},
                    {"download", {
                        {"url", ""},
                        {"sha256", ""},
                        {"filename", VISION_MODEL_FILE}
                    }},
                    {"mmproj", {
                        {"url", ""},
                        {"sha256", ""},
                        {"filename", VISION_MMPROJ_FILE},
                        {"file_size_mb", 425}
                    }}
                }
            })}
        };
    }

    /// A 448x448 image split into a red left half and a blue right half, PNG-encoded.
    static std::vector<uint8_t> makeTestImagePng()
    {
        static const std::string kPngBase64=
            "iVBORw0KGgoAAAANSUhEUgAAAcAAAAHACAIAAAC6Ry8kAAAGyElEQVR42u3UMQ0AAAzDsPInvZHo0cOSEeRILoFZGjBNAwwU"
            "DBQDBQMFA8VAwUAxUDBQMFAMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQMFAMFA8VAwUDBQDFQ"
            "MFAMFAwUDBQDBQPFQMFAwUAxUDBQDBQMFAwUAwUDxUDBQMFAMVAwUNAAAwUDxUDBQMFAMVAwUAwUDBQMFAMFA8VAwUDBQDFQ"
            "MFAMFAwUDBQDBQPFQMFAwUAxUDBQDBQMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQDBQMFA8VA"
            "wUAxUDBQMFAMFAwUBMBAwUAxUDBQMFAMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQDBQMFA8VA"
            "wUDBQDFQMFAMFAwUDBQDBQPFQMFAwUAxUDBQDBQMFAwUAwUDxUDBQMFAMVAwUAwUDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQD"
            "BQMFA8VAwUAxUDBQMFAMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQMFAMVAwUAwUDBQMFAMFA8VAwUDBQDFQMFAM"
            "FAwUDBQDBQPFQMFAwUAxUDBQDBQMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQDBQMFA8VAwUAx"
            "UDBQMFAMFAwUAwUDBQPFQMFAwUAxUDBQDBQMFAwUAwUDxUDBQMFAMVAwUAwUDBQMFAMFA8VAwUDBQDFQMFAMFAwUDBQDBQMF"
            "A8VAwUAxUDBQMFAMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQD1QADBQPFQMFAwUAxUDBQDBQM"
            "FAwUAwUDxUDBQMFAMVAwUAwUDBQMFAMFA8VAwUDBQDFQMFAMFAwUDBQDBQMFA8VAwUAxUDBQMFAMFAwUAwUDBQPFQMFAMVAw"
            "UDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQD1QADBQPFQMFAwUAxUDBQDBQMFAwUAwUDxUDBQMFAMVAwUAwUDBQMFAMFA8VA"
            "wUDBQDFQMFAMFAwUDBQDBQMFA8VAwUAxUDBQMFAMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQD"
            "1QADBQPFQMFAwUAxUDBQDBQMFAwUAwUDxUDBQMFAMVAwUAwUDBQMFAMFA8VAwUDBQDFQMFAMFAwUDBQDBQMFA8VAwUAxUDBQ"
            "MFAMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQ0wEDBQDFQMFAwUAwUDBQDBQMFA8VAwUAxUDBQ"
            "MFAMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQMFAMVAwUAwUDBQMFAMFA8VAwUDBQDFQMFAMFAwUDBQDBQPFQMFA"
            "wUAxUDBQDBQMFAwUAwUDBQ0wUDBQDBQMFAwUAwUDxUDBQMFAMVAwUAwUDBQMFAMFA8VAwUDBQDFQMFAMFAwUDBQDBQPFQMFA"
            "wUAxUDBQMFAMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQDBQMFA8VAwUBBAAwUDBQDBQMFA8VA"
            "wUAxUDBQMFAMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQMFAMFA8VAwUDBQDFQMFAMFAwUDBQD"
            "BQPFQMFAwUAxUDBQDBQMFAwUAwUDxUDBQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQDBQMFA8VAwUAxUDBQMFAM"
            "FAwUAwUDBQPFQMFAMVAwUDBQDBQMFAwUAwUDxUDBQMFAMVAwUAwUDBQMFAMFA8VAwUDBQDFQMFAMFAwUDBQDBQPFQMFAwUAx"
            "UDBQMFAMFAwUAwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQMFAwUAwUDBQDBQMFA8VAwUAxUDBQMFAMFAwUDBQDBQPF"
            "QMFAwUAxUDBQDBQMFAwUAwUDxUDBQMFAMVAwUAwUDBQMFAMFA8VAwUDBQDFQMFAwUAwUDBQDBQMFA8VAwUAxUDBQMFAMFAwU"
            "AwUDBQPFQMFAMVAwUDBQDBQMFAMFAwUDxUDBQDFQDTBQMFAMFAwUDBQDBQPFQMFAwUAxUDBQDBQMFAwUAwUDxUDBQMFAMVAw"
            "UAwUDBQMFAMFA8VAwUDBQDFQMFAwUAwUDBQDBQMFA8VAwUAxUDBQMFAMFHoeCa0dxJXzMXgAAAAASUVORK5CYII=";

        std::vector<uint8_t> png;
        base64Decode(kPngBase64, png);
        return png;
    }
};

TEST_F(LlamaVisionTest, ProjectorLoadsWithModel)
{
    ErrorCode loadResult=ModelRuntime::instance().loadModel(VISION_MODEL_NAME, "Q4_K_M", 8192);
    ASSERT_EQ(loadResult, ErrorCode::Success);

    std::optional<LoadedModel> state=ModelRuntime::instance().getModelState(VISION_MODEL_NAME);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Loaded);
    EXPECT_NE(state->llamaModel, nullptr);
    EXPECT_NE(state->llamaCtx, nullptr);
    EXPECT_NE(state->mtmdCtx, nullptr)<<"multimodal projector was not loaded";
    EXPECT_NE(ModelRuntime::instance().getMtmdContext(VISION_MODEL_NAME), nullptr);
}

TEST_F(LlamaVisionTest, DescribesAnImage)
{
    ASSERT_EQ(ModelRuntime::instance().loadModel(VISION_MODEL_NAME, "Q4_K_M", 8192), ErrorCode::Success);

    std::vector<uint8_t> png=makeTestImagePng();
    ASSERT_FALSE(png.empty());

    ContentPart textPart;
    textPart.type="text";
    textPart.text="What colors are in this image? Answer in a few words.";

    ContentPart imagePart;
    imagePart.type="image";
    imagePart.mimeType="image/png";
    imagePart.imageData=png;

    Message message;
    message.role="user";
    message.content=textPart.text;
    message.parts={textPart, imagePart};

    CompletionRequest request;
    request.model=VISION_MODEL_NAME;
    request.max_tokens=48;
    request.temperature=0.0;
    request.messages={message};

    Llama provider;
    CompletionResponse response;
    std::optional<ModelInfo> info=ModelManager::instance().getModelInfo(VISION_MODEL_NAME);
    ASSERT_TRUE(info.has_value());

    ErrorCode result=provider.completion(request, *info, response);
    ASSERT_EQ(result, ErrorCode::Success)<<provider.lastErrorDetail();

    EXPECT_FALSE(response.text.empty());
    // The image expands to far more tokens than the short text prompt.
    EXPECT_GT(response.usage.prompt_tokens, 100);

    // The image is half red, half blue — a model that actually saw it says so.
    std::string lower=response.text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    EXPECT_NE(lower.find("red"), std::string::npos)<<"response: "<<response.text;
    EXPECT_NE(lower.find("blue"), std::string::npos)<<"response: "<<response.text;
}

TEST_F(LlamaVisionTest, TextOnlyRequestStillWorks)
{
    ASSERT_EQ(ModelRuntime::instance().loadModel(VISION_MODEL_NAME, "Q4_K_M", 8192), ErrorCode::Success);

    CompletionRequest request;
    request.model=VISION_MODEL_NAME;
    request.max_tokens=32;
    request.temperature=0.0;
    request.messages={{"user", "What is 2+2? Answer with just the number."}};

    Llama provider;
    CompletionResponse response;
    std::optional<ModelInfo> info=ModelManager::instance().getModelInfo(VISION_MODEL_NAME);
    ASSERT_TRUE(info.has_value());

    ASSERT_EQ(provider.completion(request, *info, response), ErrorCode::Success)
        <<provider.lastErrorDetail();
    EXPECT_FALSE(response.text.empty());
}

} // namespace arbiterAI
