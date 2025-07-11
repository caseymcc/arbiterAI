#include "arbiterAI/providers/llama.h"

#include "arbiterAI/providers/llamaInterface.h"
#include "arbiterAI/modelManager.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <thread>
#include <future>
#include <openssl/sha.h>
#include <cpr/cpr.h>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include <atomic>

namespace arbiterAI
{

using json=nlohmann::json;

/**
 * @brief Construct a new Llama provider
 */
Llama::Llama() :
    BaseProvider("llama")
{
}

DownloadStatus Llama::getDownloadStatus(const std::string &modelName, std::string &error)
{
    std::lock_guard<std::mutex> lock(m_downloadMutex);
    auto it=m_downloadStatus.find(modelName);
    if(it!=m_downloadStatus.end())
    {
        error=it->second.error;
        return it->second.status;
    }
    return DownloadStatus::Completed;
}

/**
 * @brief Destroy the Llama provider
 */
Llama::~Llama()
{
}

/**
 * @brief Initialize Llama provider with models
 * @param models Vector of ModelInfo containing model configurations
 */
void Llama::initialize(const std::vector<ModelInfo> &models)
{
    LlamaInterface::instance().setModels(models);
}

/**
 * @brief Perform text completion using Llama model
 * @param request Completion parameters
 * @param[out] response Completion results
 * @return ErrorCode indicating success or failure
 */
ErrorCode Llama::completion(const CompletionRequest &request,
    CompletionResponse &response)
{
    LlamaInterface &llamaInterface=LlamaInterface::instance();

    if(!llamaInterface.isLoaded(request.model))
    {
        ErrorCode errorCode=llamaInterface.loadModel(request.model);

        if(errorCode!=ErrorCode::Success)
            return errorCode;
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

/**
 * @brief Perform streaming text completion
 * @param request Completion parameters
 * @param callback Function to receive streaming chunks
 * @return ErrorCode indicating success or failure
 */
ErrorCode Llama::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    LlamaInterface &llamaInterface=LlamaInterface::instance();

    if(!llamaInterface.isLoaded(request.model))
    {
        ErrorCode errorCode=llamaInterface.loadModel(request.model);

        if(errorCode!=ErrorCode::Success)
            return errorCode;
    }

    std::string prompt;

    for(const auto &msg:request.messages)
    {
        prompt+=msg.content;
    }

    return llamaInterface.streamingCompletion(prompt, callback);
}

/**
 * @brief Generate embeddings using Llama model
 * @param request Embedding generation parameters
 * @param[out] response Generated embeddings
 * @return ErrorCode indicating success or failure
 */
ErrorCode Llama::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    LlamaInterface &llamaInterface=LlamaInterface::instance();

    if(!llamaInterface.isLoaded(request.model))
    {
        ErrorCode errorCode=llamaInterface.loadModel(request.model);

        if(errorCode!=ErrorCode::Success)
            return errorCode;
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

} // namespace arbiterAI