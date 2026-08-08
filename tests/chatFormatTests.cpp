#include <gtest/gtest.h>

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatFormat.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/modelRuntime.h"

#include <filesystem>

namespace arbiterAI
{

namespace
{

/// Load a GGUF as a throwaway model and hand back its llama_model, or nullptr
/// when the file isn't staged in model_cache/.
llama_model *loadProbeModel(const std::string &name, const std::string &file)
{
    if(!std::filesystem::exists("/models/"+file))
    {
        return nullptr;
    }

    nlohmann::json config={
        {"model", name},
        {"provider", "llama"},
        {"context_window", 4096},
        {"variants", nlohmann::json::array({{
            {"quantization", "Q4_K_M"},
            {"download", {{"url", ""}, {"sha256", ""}, {"filename", file}}}
        }})}
    };

    std::string error;
    if(!ModelManager::instance().addModelFromJson(config, error))
    {
        return nullptr;
    }
    if(ModelRuntime::instance().loadModel(name, "Q4_K_M", 4096)!=ErrorCode::Success)
    {
        return nullptr;
    }
    return ModelRuntime::instance().getLlamaModel(name);
}

} // namespace

/// These exercise common_chat against real GGUF chat templates — the point
/// being that neither model needs an api_format entry in its config for the
/// format to be identified correctly.
class ChatFormatTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ModelRuntime::reset();
        ArbiterAI::instance().initialize({"tests/config"});
    }

    void TearDown() override
    {
        ModelRuntime::instance().unloadModel(m_loaded);
        ModelRuntime::reset();
    }

    std::string m_loaded;
};

TEST_F(ChatFormatTest, RendersPromptWithTheModelsOwnTurnMarkup)
{
    m_loaded="chatformat-instruct";
    llama_model *model=loadProbeModel(m_loaded, "Qwen3VL-2B-Instruct-Q4_K_M.gguf");
    if(!model) GTEST_SKIP() << "Qwen3VL-2B-Instruct-Q4_K_M.gguf not staged in model_cache/";

    std::unique_ptr<ChatFormat> format=ChatFormat::create(model);
    ASSERT_NE(format, nullptr) << "no chat template detected";

    Message user;
    user.role="user";
    user.content="What is 2+2?";

    std::shared_ptr<ChatPrompt> prompt=format->apply({user}, {});
    ASSERT_NE(prompt, nullptr);

    EXPECT_NE(prompt->text().find("<|im_start|>"), std::string::npos)
        <<"prompt should carry the model's real turn markup";
    EXPECT_NE(prompt->text().find("What is 2+2?"), std::string::npos);
    EXPECT_EQ(prompt->formatName(), "peg-native");
}

TEST_F(ChatFormatTest, InstructModelReportsNoThinkingAndLeavesContentIntact)
{
    m_loaded="chatformat-instruct";
    llama_model *model=loadProbeModel(m_loaded, "Qwen3VL-2B-Instruct-Q4_K_M.gguf");
    if(!model) GTEST_SKIP() << "Qwen3VL-2B-Instruct-Q4_K_M.gguf not staged in model_cache/";

    std::unique_ptr<ChatFormat> format=ChatFormat::create(model);
    ASSERT_NE(format, nullptr);
    Message user;
    user.role="user";
    user.content="hi";
    std::shared_ptr<ChatPrompt> prompt=format->apply({user}, {});
    ASSERT_NE(prompt, nullptr);

    // The Instruct variant does not reason, so the template says so and a
    // <think> block is just text — splitting it would be wrong.
    EXPECT_FALSE(prompt->supportsThinking());

    ChatParseResult parsed=prompt->parse("The answer is 4.", false);
    EXPECT_EQ(parsed.content, "The answer is 4.");
    EXPECT_TRUE(parsed.reasoningContent.empty());
}

TEST_F(ChatFormatTest, ThinkingModelSplitsReasoningWithNoPerModelConfig)
{
    m_loaded="chatformat-thinking";
    llama_model *model=loadProbeModel(m_loaded, "Qwen3VL-2B-Thinking-Q4_K_M.gguf");
    if(!model) GTEST_SKIP() << "Qwen3VL-2B-Thinking-Q4_K_M.gguf not staged in model_cache/";

    std::unique_ptr<ChatFormat> format=ChatFormat::create(model);
    ASSERT_NE(format, nullptr);
    Message user;
    user.role="user";
    user.content="hi";
    std::shared_ptr<ChatPrompt> prompt=format->apply({user}, {});
    ASSERT_NE(prompt, nullptr);

    // Detected purely from the chat template — no api_format anywhere.
    EXPECT_TRUE(prompt->supportsThinking());

    ChatParseResult parsed=prompt->parse("<think>weighing it up</think>The answer is 4.", false);
    EXPECT_NE(parsed.reasoningContent.find("weighing it up"), std::string::npos)
        <<"reasoning: '"<<parsed.reasoningContent<<"' content: '"<<parsed.content<<"'";
    EXPECT_NE(parsed.content.find("The answer is 4."), std::string::npos);
    EXPECT_EQ(parsed.content.find("<think>"), std::string::npos)
        <<"the thinking block must not remain in content";
}

TEST_F(ChatFormatTest, PartialParseToleratesAnUnterminatedBlock)
{
    m_loaded="chatformat-thinking";
    llama_model *model=loadProbeModel(m_loaded, "Qwen3VL-2B-Thinking-Q4_K_M.gguf");
    if(!model) GTEST_SKIP() << "Qwen3VL-2B-Thinking-Q4_K_M.gguf not staged in model_cache/";

    std::unique_ptr<ChatFormat> format=ChatFormat::create(model);
    ASSERT_NE(format, nullptr);
    Message user;
    user.role="user";
    user.content="hi";
    std::shared_ptr<ChatPrompt> prompt=format->apply({user}, {});
    ASSERT_NE(prompt, nullptr);

    // Mid-stream the block hasn't closed yet; that must parse rather than throw,
    // and the partial thought must not surface as the answer.
    ChatParseResult parsed=prompt->parse("<think>still thinking", true);
    EXPECT_TRUE(parsed.content.empty())
        <<"partial content leaked: '"<<parsed.content<<"'";
}

TEST_F(ChatFormatTest, CreateReturnsNullForNoModel)
{
    EXPECT_EQ(ChatFormat::create(nullptr), nullptr);
}

} // namespace arbiterAI
