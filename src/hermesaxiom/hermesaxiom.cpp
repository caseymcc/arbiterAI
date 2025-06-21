#include "hermesaxiom/hermesaxiom.h"
#include "hermesaxiom/modelManager.h"
#include "hermesaxiom/providers/openai_llm.h"
#include "hermesaxiom/providers/anthropic_llm.h"
#include "hermesaxiom/providers/deepseek_llm.h"

#include <memory>

namespace hermesaxiom
{

struct Hermes
{
    static Hermes &instance()
    {
        static Hermes instance;
        return instance;
    }

    bool initialized=false;
    std::map<std::string, std::unique_ptr<BaseLLM>> llms;
};

ErrorCode initialize(const std::vector<std::filesystem::path> &configPaths)
{
    if(!ModelManager::instance().initialize(configPaths))
    {
        return ErrorCode::InvalidRequest;
    }

    Hermes::instance().initialized=true;
    return ErrorCode::Success;
}

bool doesModelNeedApiKey(const std::string &model)
{
    auto provider=ModelManager::instance().getProvider(model);
 
    if(!provider)
    {
        return false;
    }

    return *provider=="openai";
}

std::unique_ptr<BaseLLM> createLLM(const ModelInfo& modelInfo) {
    if(modelInfo.provider=="openai")
    {
        return std::make_unique<OpenAILLM>(modelInfo);
    }
    else if(modelInfo.provider=="anthropic")
    {
        return std::make_unique<AnthropicLLM>(modelInfo);
    }
    else if(modelInfo.provider=="deepseek")
    {
        return std::make_unique<DeepseekLLM>(modelInfo);
    }
    return nullptr;
}

BaseLLM* getLLM(const CompletionRequest &request, const ModelInfo &modelInfo)
{
    auto& hermes = Hermes::instance();
    
    // Check if we already have an LLM instance for this model
    auto it = hermes.llms.find(request.model);
    if (it == hermes.llms.end()) {
        // Create new LLM instance
        auto llm = createLLM(modelInfo);
        if (!llm) {
            return nullptr;
        }
        it = hermes.llms.emplace(request.model, std::move(llm)).first;
    }

    return it->second.get();
}

ErrorCode completion(const CompletionRequest &request, CompletionResponse &response)
{
    if(!Hermes::instance().initialized)
    {
        return ErrorCode::InvalidRequest;
    }

    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(request.model);
    if(!modelInfo)
    {
        return ErrorCode::UnknownModel;
    }

    BaseLLM *llm = getLLM(request, *modelInfo);
    if (!llm) {
        return ErrorCode::UnsupportedProvider;
    }

    return llm->completion(request, response);
}

ErrorCode streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string&)> callback)
{
    if(!Hermes::instance().initialized)
    {
        return ErrorCode::InvalidRequest;
    }

    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(request.model);
    if(!modelInfo)
    {
        return ErrorCode::UnknownModel;
    }

    BaseLLM *llm = getLLM(request, *modelInfo);
    if (!llm) {
        return ErrorCode::UnsupportedProvider;
    }

    return llm->streamingCompletion(request, callback);
}

} // namespace hermesaxiom
