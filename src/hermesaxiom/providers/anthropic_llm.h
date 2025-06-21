#ifndef _hermesaxiom_providers_anthropic_llm_h_
#define _hermesaxiom_providers_anthropic_llm_h_

#include "hermesaxiom/providers/base_llm.h"
#include "hermesaxiom/modelManager.h"

namespace hermesaxiom
{

class AnthropicLLM : public BaseLLM
{
public:
    AnthropicLLM(const ModelInfo& modelInfo) : m_modelInfo(modelInfo) {};

    ErrorCode completion(const CompletionRequest& request,
                        CompletionResponse& response) override;
                        
    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string&)> callback) override;

private:
    ModelInfo m_modelInfo;
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_anthropic_llm_h_
