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

void Llama::initialize(const std::vector<ModelInfo>& models)
{
    LlamaInterface::instance().setModels(models);
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
    std::vector<float> embedding;
    ErrorCode code=std::visit([&](auto &&arg)
        {
            return llamaInterface.getEmbeddings(arg, embedding, tokens_used);
        }, request.input);

    if(code==ErrorCode::Success)
    {
        response.model=request.model;
        response.usage.prompt_tokens=tokens_used;
        response.usage.total_tokens=tokens_used;
        Embedding emb;
        emb.embedding=embedding;
        response.data.push_back(emb);
    }

    return code;
}

} // namespace hermesaxiom