#include "hermesaxiom/providers/llama.h"

#include "hermesaxiom/providers/llamaInterface.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <thread>
#include <future>

namespace hermesaxiom
{

Llama::Llama() :
    BaseProvider("llama")
{
}

Llama::~Llama()
{
}

ErrorCode Llama::completion(const CompletionRequest &request,
    CompletionResponse &response)
{
    LlamaInterface &llamaInterface=LlamaInterface::instance();

    std::string error;
    DownloadStatus status=getDownloadStatus(request.model, error);
    if(status==DownloadStatus::InProgress)
    {
        return ErrorCode::ModelDownloading;
    }
    if(status==DownloadStatus::Failed)
    {
        return ErrorCode::DownloadFailed;
    }

    if(!llamaInterface.isLoaded(request.model))
    {
        if(!llamaInterface.loadModel(request.model))
            return ErrorCode::ModelNotLoaded;
    }

    std::string prompt;
    for(const auto &msg:request.messages)
    {
        prompt+=msg.content;
    }

    std::string result_text;
    ErrorCode code=llamaInterface.completion(prompt, result_text);

    if(code==ErrorCode::Success)
    {
        response.text=result_text;
        response.provider="llama";
        response.model=request.model;
    }

    return code;
}

ErrorCode Llama::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    LlamaInterface &llamaInterface=LlamaInterface::instance();

    std::string error;
    DownloadStatus status=getDownloadStatus(request.model, error);
    if(status==DownloadStatus::InProgress)
    {
        return ErrorCode::ModelDownloading;
    }
    if(status==DownloadStatus::Failed)
    {
        return ErrorCode::DownloadFailed;
    }

    if(!llamaInterface.isLoaded(request.model))
    {
        if(!llamaInterface.loadModel(request.model))
            return ErrorCode::ModelNotLoaded;
    }

    std::string prompt;
    for(const auto &msg:request.messages)
    {
        prompt+=msg.content;
    }

    return llamaInterface.streamingCompletion(prompt, callback);
}

ErrorCode Llama::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    LlamaInterface &llamaInterface=LlamaInterface::instance();

    if(!llamaInterface.isLoaded(request.model))
    {
        if(!llamaInterface.loadModel(request.model))
            return ErrorCode::ModelNotLoaded;
    }

    std::string error;
    DownloadStatus status=getDownloadStatus(request.model, error);
    if(status==DownloadStatus::InProgress)
    {
        return ErrorCode::ModelDownloading;
    }
    if(status==DownloadStatus::Failed)
    {
        return ErrorCode::DownloadFailed;
    }

    int tokens_used=0;
    ErrorCode code=llamaInterface.getEmbeddings(request.input, response.embedding, tokens_used);

    if(code==ErrorCode::Success)
    {
        response.provider="llama";
        response.model=request.model;
        response.tokens_used=tokens_used;
    }

    return code;
}

} // namespace hermesaxiom