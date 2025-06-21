#include "hermesaxiom/providers/base_llm.h"
#include <cstdlib>

namespace hermesaxiom
{

ErrorCode BaseLLM::getApiKey(const std::string &provider, std::string &apiKey)
{
    if(provider=="openai")
    {
        if(auto key=std::getenv("OPENAI_API_KEY"))
        {
            apiKey=key;
            return ErrorCode::Success;
        }
    }
    else if(provider=="anthropic")
    {
        if(auto key=std::getenv("ANTHROPIC_API_KEY"))
        {
            apiKey=key;
            return ErrorCode::Success;
        }
    }
    return ErrorCode::ApiKeyNotFound;
}

} // namespace hermesaxiom
