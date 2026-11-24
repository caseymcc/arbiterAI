#include <gtest/gtest.h>

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/providers/llama.h"
#include "arbiterAI/telemetryCollector.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/inferenceScheduler.h"

#include <llama.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
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

// ─── reasoning_budget on real models ─────────────────────────────────────
// THINK_MODEL_PATH: a GGUF of a model with single-token <think>/</think> (Qwen3
// family, e.g. Qwen3-0.6B-Q8_0); NOTHINK_MODEL_PATH: one without (Qwen2.5).
// Each test skips when its variable is unset or the file is missing.

static const std::string THINK_MODEL_NAME="reasoning-think-test";
static const std::string NOTHINK_MODEL_NAME="reasoning-nothink-test";
static const std::string BUDGET_PROMPT="What is 17 times 23? Reply with just the number.";

class LlamaReasoningBudgetTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ModelRuntime::reset();
        TelemetryCollector::reset();
        ArbiterAI::instance().initialize({"tests/config"});
    }

    void TearDown() override
    {
        InferenceScheduler::instance().shutdown();
        ModelRuntime::reset();
        TelemetryCollector::reset();
    }

    /// Registers the model in the env variable's file as \p name; false when
    /// the variable is unset or the file is missing.
    bool useModel(const char *variable, const std::string &name)
    {
        const char *path=std::getenv(variable);
        if(!path||!std::filesystem::exists(path))
            return false;

        std::filesystem::path file(path);
        nlohmann::json modelJson={
            {"model", name},
            {"provider", "llama"},
            {"ranking", 1},
            {"version", "1.1.0"},
            {"context_window", 4096},
            {"max_tokens", 2048},
            {"max_output_tokens", 1024},
            {"hardware_requirements", {{"min_system_ram_mb", 1024}, {"parameter_count", "1B"}}},
            {"context_scaling", {{"base_context", 4096}, {"max_context", 32768}, {"vram_per_1k_context_mb", 32}}},
            {"variants", nlohmann::json::array({{
                {"quantization", "test"},
                {"file_size_mb", static_cast<int>(std::filesystem::file_size(file)/(1024*1024))},
                {"min_vram_mb", 1024},
                {"recommended_vram_mb", 2048},
                {"download", {{"url", ""}, {"sha256", ""}, {"filename", file.filename().string()}}}
            }})}
        };
        std::string error;
        if(!ModelManager::instance().addModelFromJson(modelJson, error))
        {
            ADD_FAILURE()<<"addModelFromJson failed: "<<error;
            return false;
        }
        ModelRuntime::instance().setModelsDir(file.parent_path().string());
        return true;
    }

    /// An explicit load, as POST /api/models/{name}/load makes it: on a loaded
    /// model it applies the budget live.
    ErrorCode load(const std::string &name, std::optional<int> budget)
    {
        RuntimeOptions options;
        options.reasoningBudget=budget;
        return ArbiterAI::instance().loadModel(name, "test", 4096, &options);
    }

    CompletionRequest request(const std::string &name) const
    {
        CompletionRequest request;
        request.model=name;
        request.max_tokens=400;
        request.messages={{"user", BUDGET_PROMPT}};
        return request;
    }

    CompletionResponse complete(const std::string &name)
    {
        CompletionResponse response;
        EXPECT_EQ(ArbiterAI::instance().completion(request(name), response), ErrorCode::Success);
        return response;
    }

    std::string stream(const std::string &name)
    {
        std::string text;
        EXPECT_EQ(ArbiterAI::instance().streamingCompletion(request(name),
            [&text](const std::string &chunk) { text+=chunk; }), ErrorCode::Success);
        return text;
    }

    /// Runs the request through the server's pipeline (tokenizer and
    /// accelerator stages).
    std::shared_ptr<InferenceJob> schedule(const std::string &name)
    {
        InferenceScheduler &scheduler=InferenceScheduler::instance();
        if(!scheduler.isRunning())
            scheduler.initialize({});
        std::shared_ptr<InferenceJob> job=scheduler.submit(request(name), false);
        std::unique_lock<std::mutex> lock(job->completionMutex);
        EXPECT_TRUE(job->completionCv.wait_for(lock, std::chrono::minutes(5), [&job]() { return job->complete.load(); }));
        return job;
    }

    int tokenCount(const std::string &name, const std::string &text)
    {
        const llama_vocab *vocab=llama_model_get_vocab(ModelRuntime::instance().getLlamaModel(name));
        return -llama_tokenize(vocab, text.c_str(), text.size(), nullptr, 0, false, true);
    }
};

TEST_F(LlamaReasoningBudgetTest, ThinkModelIsDetectedAtLoad)
{
    if(!useModel("THINK_MODEL_PATH", THINK_MODEL_NAME))
        GTEST_SKIP()<<"THINK_MODEL_PATH not set or file missing";
    ASSERT_EQ(load(THINK_MODEL_NAME, std::nullopt), ErrorCode::Success);

    std::optional<LoadedModel> state=ModelRuntime::instance().getModelState(THINK_MODEL_NAME);
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->thinkTokens.valid());
    EXPECT_NE(state->thinkTokens.open, state->thinkTokens.close);
}

TEST_F(LlamaReasoningBudgetTest, ZeroTurnsThinkingOffOnALoadedModel)
{
    if(!useModel("THINK_MODEL_PATH", THINK_MODEL_NAME))
        GTEST_SKIP()<<"THINK_MODEL_PATH not set or file missing";
    ASSERT_EQ(load(THINK_MODEL_NAME, std::nullopt), ErrorCode::Success);
    CompletionResponse thinking=complete(THINK_MODEL_NAME);
    EXPECT_NE(thinking.text.find("<think>"), std::string::npos)<<thinking.text;

    // applied live: no reload between the two requests
    ASSERT_EQ(load(THINK_MODEL_NAME, 0), ErrorCode::Success);
    CompletionResponse off=complete(THINK_MODEL_NAME);
    EXPECT_EQ(off.text.find("<think>"), std::string::npos)<<off.text;
    EXPECT_EQ(off.text.find("</think>"), std::string::npos)<<off.text;
    EXPECT_NE(off.text.find("391"), std::string::npos)<<off.text;
    EXPECT_LT(off.usage.completion_tokens*4, thinking.usage.completion_tokens)
        <<"off: "<<off.usage.completion_tokens<<", unset: "<<thinking.usage.completion_tokens;

    EXPECT_EQ(stream(THINK_MODEL_NAME), off.text);
}

TEST_F(LlamaReasoningBudgetTest, ZeroTurnsThinkingOffOnTheSchedulerPath)
{
    if(!useModel("THINK_MODEL_PATH", THINK_MODEL_NAME))
        GTEST_SKIP()<<"THINK_MODEL_PATH not set or file missing";
    ASSERT_EQ(load(THINK_MODEL_NAME, 0), ErrorCode::Success);

    std::shared_ptr<InferenceJob> job=schedule(THINK_MODEL_NAME);
    EXPECT_EQ(job->result, ErrorCode::Success);
    EXPECT_EQ(job->resultText.find("</think>"), std::string::npos)<<job->resultText;
    EXPECT_NE(job->resultText.find("391"), std::string::npos)<<job->resultText;
    EXPECT_LT(job->completionTokens.load(), 40);
}

TEST_F(LlamaReasoningBudgetTest, CapClosesTheBlockThenAnswers)
{
    if(!useModel("THINK_MODEL_PATH", THINK_MODEL_NAME))
        GTEST_SKIP()<<"THINK_MODEL_PATH not set or file missing";
    const int cap=32;
    ASSERT_EQ(load(THINK_MODEL_NAME, cap), ErrorCode::Success);

    CompletionResponse capped=complete(THINK_MODEL_NAME);
    const size_t open=capped.text.find("<think>");
    const size_t close=capped.text.find(ReasoningBudget::closeText());
    ASSERT_NE(open, std::string::npos)<<capped.text;
    ASSERT_NE(close, std::string::npos)<<capped.text;
    EXPECT_EQ(capped.text.find("</think>", close+ReasoningBudget::closeText().size()), std::string::npos)<<capped.text;

    const std::string thought=capped.text.substr(open+ReasoningBudget::openTag().size(), close-open-ReasoningBudget::openTag().size());
    EXPECT_NEAR(tokenCount(THINK_MODEL_NAME, thought), cap, 4)<<thought;
    EXPECT_FALSE(capped.text.substr(close+ReasoningBudget::closeText().size()).empty())<<capped.text;

    // the injected close is counted: completion tokens are what the KV cache
    // holds beyond the prompt
    std::vector<int32_t> *kv=ModelRuntime::instance().kvCacheTokens(THINK_MODEL_NAME);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(capped.usage.completion_tokens, static_cast<int>(kv->size())-capped.usage.prompt_tokens);

    // streaming runs the other generation loop and shows the same forced close
    EXPECT_EQ(stream(THINK_MODEL_NAME), capped.text);

    std::shared_ptr<InferenceJob> job=schedule(THINK_MODEL_NAME);
    EXPECT_EQ(job->resultText, capped.text);
}

TEST_F(LlamaReasoningBudgetTest, ModelWithoutThinkTokensIgnoresTheBudget)
{
    if(!useModel("NOTHINK_MODEL_PATH", NOTHINK_MODEL_NAME))
        GTEST_SKIP()<<"NOTHINK_MODEL_PATH not set or file missing";
    ASSERT_EQ(load(NOTHINK_MODEL_NAME, std::nullopt), ErrorCode::Success);
    EXPECT_FALSE(ModelRuntime::instance().getModelState(NOTHINK_MODEL_NAME)->thinkTokens.valid());
    CompletionResponse unset=complete(NOTHINK_MODEL_NAME);

    for(int budget:{0, 8})
    {
        ASSERT_EQ(load(NOTHINK_MODEL_NAME, budget), ErrorCode::Success);
        CompletionResponse response=complete(NOTHINK_MODEL_NAME);
        EXPECT_EQ(response.text, unset.text)<<"budget "<<budget;
        EXPECT_EQ(response.usage.prompt_tokens, unset.usage.prompt_tokens)<<"budget "<<budget;
        EXPECT_EQ(response.usage.completion_tokens, unset.usage.completion_tokens)<<"budget "<<budget;
    }
}

} // namespace arbiterAI
