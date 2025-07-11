#include "arbiterAI/providers/llamaInterface.h"

#include "arbiterAI/modelManager.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <thread>

namespace arbiterAI
{

LlamaInterface &LlamaInterface::instance()
{
    static LlamaInterface instance;
    return instance;
}

void LlamaInterface::setModels(const std::vector<ModelInfo> &models)
{
    for(const ModelInfo &model:models)
    {
        LlamaModelInfo llamaModel;
        llamaModel.modelInfo=model;

        if(model.download)
        {
            llamaModel.downloadUrl=model.download->url;
            llamaModel.fileHash=model.download->sha256;
        }

        if(model.filePath)
        {
            llamaModel.filePath=model.filePath;
        }
        else if(model.download)
        {
            // If no file path is provided, construct it from the model name
            llamaModel.filePath="/models/"+model.model;
        }

        m_llamaModels.push_back(llamaModel);
    }
}

LlamaInterface::LlamaInterface()
{
    initialize();
}

LlamaInterface::~LlamaInterface()
{
    if(m_ctx)
    {
        llama_free(m_ctx);
    }
    if(m_model)
    {
        llama_model_free(m_model);
    }
    if(m_initialized)
    {
        llama_backend_free();
    }
}

void LlamaInterface::initialize()
{
    if(!m_initialized)
    {
        llama_backend_init();
        m_initialized=true;
    }
}

ErrorCode LlamaInterface::completion(const CompletionRequest &request, std::string &result)
{
    std::string prompt;
    for(const auto &msg:request.messages)
    {
        prompt+=msg.content;
    }

    // tokenize the prompt
    std::vector<llama_token> tokens_list(prompt.size());
    int n_tokens=llama_tokenize(llama_model_get_vocab(m_model), prompt.c_str(), prompt.length(), tokens_list.data(), tokens_list.size(), true, false);
    if(n_tokens<0)
    {
        spdlog::error("Failed to tokenize prompt");
        return ErrorCode::GenerationError;
    }
    tokens_list.resize(n_tokens);

    llama_batch batch=llama_batch_init(512, 0, 1);

    batch.n_tokens=n_tokens;
    for(int32_t i=0; i<batch.n_tokens; i++)
    {
        batch.token[i]=tokens_list[i];
        batch.pos[i]=i;
        batch.n_seq_id[i]=1;
        batch.seq_id[i][0]=0;
        batch.logits[i]=0;
    }
    batch.logits[batch.n_tokens-1]=1;

    if(llama_decode(m_ctx, batch)!=0)
    {
        spdlog::error("llama_decode failed");
        llama_batch_free(batch);
        return ErrorCode::GenerationError;
    }

    int n_cur=n_tokens;
    int n_len=m_modelInfo->maxOutputTokens;
    auto n_vocab=llama_vocab_n_tokens(llama_model_get_vocab(m_model));
    std::vector<llama_token_data> candidates;
    candidates.reserve(n_vocab);

    struct llama_sampler_chain_params params={};
    struct llama_sampler *sampler_chain=llama_sampler_chain_init(params);

    llama_sampler_chain_add(sampler_chain, llama_sampler_init_penalties(
        -1,
        1.0f,
        request.frequency_penalty.value_or(0.0f),
        request.presence_penalty.value_or(0.0f)
    ));
    if(request.top_p.has_value())
    {
        llama_sampler_chain_add(sampler_chain, llama_sampler_init_top_p(*request.top_p, 1));
    }
    if(request.temperature.has_value())
    {
        llama_sampler_chain_add(sampler_chain, llama_sampler_init_temp(*request.temperature));
    }
    llama_sampler_chain_add(sampler_chain, llama_sampler_init_greedy());

    for(const auto &token:tokens_list)
    {
        llama_sampler_accept(sampler_chain, token);
    }

    for(int i=0; i<n_len; ++i)
    {
        const llama_token next_token=llama_sampler_sample(sampler_chain, m_ctx, batch.n_tokens-1);
        llama_sampler_accept(sampler_chain, next_token);

        if(next_token==llama_vocab_eos(llama_model_get_vocab(m_model)))
        {
            break;
        }

        if(request.stop.has_value())
        {
            bool stop_sequence_found=false;
            for(const auto &stop_word:*request.stop)
            {
                if(result.size()>=stop_word.size()&&result.substr(result.size()-stop_word.size())==stop_word)
                {
                    stop_sequence_found=true;
                    break;
                }
            }
            if(stop_sequence_found)
            {
                break;
            }
        }

        char piece[16];
        int len=llama_token_to_piece(llama_model_get_vocab(m_model), next_token, piece, sizeof(piece), 0, false);
        if(len>0)
        {
            result.append(piece, len);
        }

        batch.n_tokens=1;
        batch.token[0]=next_token;
        batch.pos[0]=n_cur;
        batch.logits[0]=1;

        n_cur++;

        if(llama_decode(m_ctx, batch)!=0)
        {
            spdlog::error("llama_decode failed");
            llama_sampler_free(sampler_chain);
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
    }
    llama_sampler_free(sampler_chain);

    llama_batch_free(batch);

    return ErrorCode::Success;
}

ErrorCode LlamaInterface::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    std::string prompt;
    for(const auto &msg:request.messages)
    {
        prompt+=msg.content;
    }

    // tokenize the prompt
    std::vector<llama_token> tokens_list(prompt.size());
    int n_tokens=llama_tokenize(llama_model_get_vocab(m_model), prompt.c_str(), prompt.length(), tokens_list.data(), tokens_list.size(), true, false);
    if(n_tokens<0)
    {
        spdlog::error("Failed to tokenize prompt");
        return ErrorCode::GenerationError;
    }
    tokens_list.resize(n_tokens);

    llama_batch batch=llama_batch_init(512, 0, 1);

    batch.n_tokens=n_tokens;
    for(int32_t i=0; i<batch.n_tokens; i++)
    {
        batch.token[i]=tokens_list[i];
        batch.pos[i]=i;
        batch.n_seq_id[i]=1;
        batch.seq_id[i][0]=0;
        batch.logits[i]=0;
    }
    batch.logits[batch.n_tokens-1]=1;

    if(llama_decode(m_ctx, batch)!=0)
    {
        spdlog::error("llama_decode failed");
        llama_batch_free(batch);
        return ErrorCode::GenerationError;
    }

    int n_cur=n_tokens;
    int n_len=m_modelInfo->maxOutputTokens;
    auto n_vocab=llama_vocab_n_tokens(llama_model_get_vocab(m_model));
    std::vector<llama_token_data> candidates;
    candidates.reserve(n_vocab);

    struct llama_sampler_chain_params params={};
    struct llama_sampler *sampler_chain=llama_sampler_chain_init(params);

    llama_sampler_chain_add(sampler_chain, llama_sampler_init_penalties(
        -1,
        1.0f,
        request.frequency_penalty.value_or(0.0f),
        request.presence_penalty.value_or(0.0f)
    ));
    if(request.top_p.has_value())
    {
        llama_sampler_chain_add(sampler_chain, llama_sampler_init_top_p(*request.top_p, 1));
    }
    if(request.temperature.has_value())
    {
        llama_sampler_chain_add(sampler_chain, llama_sampler_init_temp(*request.temperature));
    }
    llama_sampler_chain_add(sampler_chain, llama_sampler_init_greedy());

    for(const auto &token:tokens_list)
    {
        llama_sampler_accept(sampler_chain, token);
    }

    for(int i=0; i<n_len; ++i)
    {
        const llama_token next_token=llama_sampler_sample(sampler_chain, m_ctx, batch.n_tokens-1);
        llama_sampler_accept(sampler_chain, next_token);

        if(next_token==llama_vocab_eos(llama_model_get_vocab(m_model)))
        {
            break;
        }

        if(request.stop.has_value())
        {
            bool stop_sequence_found=false;
            std::string current_output;
            char piece[16];
            int len=llama_token_to_piece(llama_model_get_vocab(m_model), next_token, piece, sizeof(piece), 0, false);
            if(len>0)
            {
                current_output.append(piece, len);
            }

            for(const auto &stop_word:*request.stop)
            {
                if(current_output.find(stop_word)!=std::string::npos)
                {
                    stop_sequence_found=true;
                    break;
                }
            }
            if(stop_sequence_found)
            {
                break;
            }
        }
        char piece[16];
        int len=llama_token_to_piece(llama_model_get_vocab(m_model), next_token, piece, sizeof(piece), 0, false);
        if(len>0)
        {
            callback(std::string(piece, len));
        }

        batch.n_tokens=1;
        batch.token[0]=next_token;
        batch.pos[0]=n_cur;
        batch.logits[0]=1;

        n_cur++;

        if(llama_decode(m_ctx, batch)!=0)
        {
            spdlog::error("llama_decode failed");
            llama_sampler_free(sampler_chain);
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
    }
    llama_sampler_free(sampler_chain);

    llama_batch_free(batch);

    return ErrorCode::Success;
}

ErrorCode LlamaInterface::getEmbeddings(const std::string &input,
    std::vector<float> &embedding, int &tokens_used)
{
    if(!m_ctx)
    {
        return ErrorCode::ModelNotLoaded;
    }

    // tokenize the prompt
    std::vector<llama_token> tokens_list(input.size());
    int n_tokens=llama_tokenize(llama_model_get_vocab(m_model), input.c_str(), input.length(), tokens_list.data(), tokens_list.size(), true, false);
    if(n_tokens<0)
    {
        spdlog::error("Failed to tokenize prompt");
        return ErrorCode::GenerationError;
    }
    tokens_list.resize(n_tokens);

    llama_batch batch=llama_batch_init(n_tokens, 0, 1);

    batch.n_tokens=n_tokens;
    for(int32_t i=0; i<batch.n_tokens; i++)
    {
        batch.token[i]=tokens_list[i];
        batch.pos[i]=i;
        batch.n_seq_id[i]=1;
        batch.seq_id[i][0]=0;
        batch.logits[i]=0;
    }
    batch.logits[batch.n_tokens-1]=1;

    if(llama_decode(m_ctx, batch)!=0)
    {
        spdlog::error("llama_decode failed");
        llama_batch_free(batch);
        return ErrorCode::GenerationError;
    }

    const float *embeddings_ptr=llama_get_embeddings(m_ctx);
    if(!embeddings_ptr)
    {
        spdlog::error("llama_get_embeddings failed");
        llama_batch_free(batch);
        return ErrorCode::GenerationError;
    }

    int n_embed=llama_model_n_embd(m_model);
    embedding.assign(embeddings_ptr, embeddings_ptr+n_embed);

    llama_batch_free(batch);

    tokens_used=n_tokens;

    return ErrorCode::Success;
}

ErrorCode LlamaInterface::getEmbeddings(const std::vector<std::string> &input,
    std::vector<float> &embedding, int &tokens_used)
{
    std::string combined_input;
    for(const auto &s:input)
    {
        combined_input+=s;
    }
    return getEmbeddings(combined_input, embedding, tokens_used);
}

bool LlamaInterface::isModelDownloaded(const LlamaModelInfo &modelInfo) const
{
    if(modelInfo.filePath)
    {
        return std::filesystem::exists(*modelInfo.filePath);
    }
    return false;
}

void LlamaInterface::downloadModel(LlamaModelInfo &modelInfo)
{
    modelInfo.downloadStatus=DownloadStatus::InProgress;
    m_downloadFutures[modelInfo.modelInfo.model]=std::async(std::launch::async, [this, &modelInfo]()
        {
            if(modelInfo.downloadUrl&&modelInfo.filePath)
            {
                std::future<bool> downloadFuture=m_downloader.downloadModel(*modelInfo.downloadUrl, *modelInfo.filePath, modelInfo.fileHash);
                if(downloadFuture.get())
                {
                    modelInfo.downloadStatus=DownloadStatus::Completed;
                }
                else
                {
                    modelInfo.downloadStatus=DownloadStatus::Failed;
                    modelInfo.downloadError="Failed to download model";
                }
            }
            else
            {
                modelInfo.downloadStatus=DownloadStatus::Failed;
                modelInfo.downloadError="Missing download URL or file path";
            }
        });
}

DownloadStatus LlamaInterface::getDownloadStatus(const std::string &modelName, std::string &error)
{
    auto it=std::find_if(m_llamaModels.begin(), m_llamaModels.end(),
        [&](const LlamaModelInfo &info)
        {
            return info.modelInfo.model==modelName;
        });

    if(it!=m_llamaModels.end())
    {
        error=it->downloadError;
        return it->downloadStatus;
    }

    return DownloadStatus::Failed;
}

bool LlamaInterface::isLoaded(const std::string &modelName) const
{
    if(!m_modelInfo||m_modelInfo->model!=modelName)
    {
        return false;
    }
    return m_model!=nullptr&&m_ctx!=nullptr;
}

ErrorCode LlamaInterface::loadModel(const std::string &modelName)
{
    if(m_modelInfo&&m_modelInfo->model==modelName)
    {
        return ErrorCode::Success;
    }

    auto modelInfo=ModelManager::instance().getModelInfo(modelName);

    if(!modelInfo)
    {
        spdlog::error("Llama model not found in ModelManager: {}", modelName);
        return ErrorCode::ModelNotFound;
    }

    if(!modelInfo->filePath)
    {
        auto it=std::find_if(m_llamaModels.begin(), m_llamaModels.end(),
            [&](const LlamaModelInfo &info)
            {
                return info.modelInfo.model==modelName;
            });

        if(it==m_llamaModels.end())
        {
            spdlog::error("Llama model not found in llama config: {}", modelName);
            return ErrorCode::ModelNotFound;
        }

        LlamaModelInfo &llamaModelInfo=*it;

        if(llamaModelInfo.downloadStatus==DownloadStatus::InProgress)
        {
            return ErrorCode::ModelDownloading;
        }
        if(llamaModelInfo.downloadStatus==DownloadStatus::Failed)
        {
            spdlog::error("Llama model download failed: {}", llamaModelInfo.downloadError);
            return ErrorCode::ModelDownloadFailed;
        }

        if(!isModelDownloaded(llamaModelInfo))
        {
            downloadModel(llamaModelInfo);
            return ErrorCode::ModelDownloading;
        }
        modelInfo->filePath=llamaModelInfo.filePath;
    }

    m_modelInfo=modelInfo;

    auto mparams=llama_model_default_params();
    const char *modelPath=modelInfo->filePath->c_str();
    m_model=llama_model_load_from_file(modelPath, mparams);

    if(!m_model)
    {
        spdlog::error("Failed to load model: {}", modelPath);
        return ErrorCode::ModelLoadError;
    }

    auto cparams=llama_context_default_params();

    cparams.n_ctx=m_modelInfo->contextWindow;
    cparams.n_threads=std::thread::hardware_concurrency();
    cparams.n_threads_batch=std::thread::hardware_concurrency();

    m_ctx=llama_init_from_model(m_model, cparams);

    if(!m_ctx)
    {
        spdlog::error("Failed to create context for model: {}", m_modelInfo->model);
        return ErrorCode::ModelLoadError;
    }
    return ErrorCode::Success;
}


} // namespace arbiterAI