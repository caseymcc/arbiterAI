#ifndef _hermesaxiom_providers_llama_llm_h_
#define _hermesaxiom_providers_llama_llm_h_

#include "hermesaxiom/providers/base_llm.h"
#include "hermesaxiom/modelManager.h"

#include <llama.h>

namespace hermesaxiom
{

class LlamaLLM : public BaseLLM
{
public:
    LlamaLLM(const ModelInfo &modelInfo);
    ~LlamaLLM();

    ErrorCode completion(const CompletionRequest &request,
                        CompletionResponse &response) override;
    
    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string&)> callback) override;

private:
    void loadModel();

    ModelInfo m_modelInfo;
    llama_model* m_model = nullptr;
    llama_context* m_ctx = nullptr;
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_llama_llm_h_