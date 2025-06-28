#ifndef _hermesaxiom_providers_baseProvider_h_
#define _hermesaxiom_providers_baseProvider_h_

#include "hermesaxiom/hermesaxiom.h"
#include <functional>

namespace hermesaxiom
{

class BaseProvider
{
public:
    BaseProvider(const std::string provider);
    virtual ~BaseProvider()=default;

    virtual ErrorCode completion(const CompletionRequest &request,
        CompletionResponse &response)=0;

    virtual ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback)=0;

    virtual ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response)=0;

    virtual DownloadStatus getDownloadStatus(const std::string &modelName, std::string &error);

protected:
    ErrorCode getApiKey(const std::string &modelName,
        const std::optional<std::string> &requestApiKey, std::string &apiKey);

protected:
    std::string m_provider;
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_baseProvider_h_
