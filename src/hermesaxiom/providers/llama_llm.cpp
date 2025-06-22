#include "hermesaxiom/providers/llama_llm.h"

#include "hermesaxiom/providers/llama_provider.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <thread>
#include <future>

namespace hermesaxiom
{

LlamaLLM::LlamaLLM()
    : BaseLLM("llama")
{
}

LlamaLLM::~LlamaLLM()
{
}

ErrorCode LlamaLLM::completion(const CompletionRequest &request,
    CompletionResponse &response)
{
    LlamaProvider &llamaProvider=LlamaProvider::instance();

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

    if(!llamaProvider.isLoaded(request.model))
    {
        if(!llamaProvider.loadModel(request.model))
            return ErrorCode::ModelNotLoaded;
    }

    std::string prompt;
    for(const auto &msg:request.messages)
    {
        prompt+=msg.content;
    }

    std::string result_text;
    ErrorCode code=llamaProvider.completion(prompt, result_text);

    if(code==ErrorCode::Success)
    {
        response.text=result_text;
        response.provider="llama";
        response.model=request.model;
    }

    return code;
}

ErrorCode LlamaLLM::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    LlamaProvider &llamaProvider=LlamaProvider::instance();

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

    if(!llamaProvider.isLoaded(request.model))
    {
        if(!llamaProvider.loadModel(request.model))
            return ErrorCode::ModelNotLoaded;
    }

    std::string prompt;
    for(const auto &msg:request.messages)
    {
        prompt+=msg.content;
    }

    return llamaProvider.streamingCompletion(prompt, callback);
}

ErrorCode LlamaLLM::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    LlamaProvider &llamaProvider=LlamaProvider::instance();

    if(!llamaProvider.isLoaded(request.model))
    {
        if(!llamaProvider.loadModel(request.model))
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
    ErrorCode code=llamaProvider.getEmbeddings(request.input, response.embedding, tokens_used);

    if(code==ErrorCode::Success)
    {
        response.provider="llama";
        response.model=request.model;
        response.tokens_used=tokens_used;
    }

    return code;
}

} // namespace hermesaxiom