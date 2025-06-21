#ifndef _hermesaxiom_providers_base_llm_h_
#define _hermesaxiom_providers_base_llm_h_

#include "hermesaxiom/hermesaxiom.h"
#include <functional>

namespace hermesaxiom
{

class BaseLLM
{
public:
    virtual ~BaseLLM()=default;

    virtual ErrorCode completion(const CompletionRequest &request,
        CompletionResponse &response)=0;
    
    virtual ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string&)> callback)=0;

protected:
    ErrorCode getApiKey(const std::string &provider, std::string &apiKey);
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_base_llm_h_
