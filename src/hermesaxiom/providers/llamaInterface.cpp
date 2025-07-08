#include "hermesaxiom/providers/llamaInterface.h"

#include "hermesaxiom/modelManager.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <thread>

namespace hermesaxiom
{

LlamaInterface &LlamaInterface::instance()
{
    static LlamaInterface instance;
    return instance;
}

void LlamaInterface::setModels(const std::vector<ModelInfo> &models)
{
    for (const auto& model : models) {
        LlamaModelInfo llamaModel;
        llamaModel.modelInfo = model;
        if (model.downloadUrl) {
            llamaModel.downloadUrl = model.downloadUrl;
        }
        if (model.filePath) {
            llamaModel.filePath = model.filePath;
        } else if (model.downloadUrl) {
            // If no file path is provided, construct it from the model name
            llamaModel.filePath = "/models/" + model.model;
        }
        if (model.fileHash) {
            llamaModel.fileHash = model.fileHash;
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

ErrorCode LlamaInterface::completion(const std::string &prompt, std::string &result)
{
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

    for(int i=0; i<n_len; ++i)
    {
        auto *logits=llama_get_logits_ith(m_ctx, batch.n_tokens-1);

        candidates.clear();
        for(llama_token token_id=0; token_id<n_vocab; token_id++)
        {
            candidates.push_back({ token_id, logits[token_id], 0.0f });
        }

        auto max_it=std::max_element(candidates.begin(), candidates.end(),
            [](const llama_token_data &a, const llama_token_data &b)
            {
                return a.logit<b.logit;
            });
        const llama_token next_token=max_it->id;

        if(next_token==llama_token_eos(llama_model_get_vocab(m_model)))
        {
            break;
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
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
    }

    llama_batch_free(batch);

    return ErrorCode::Success;
}

ErrorCode LlamaInterface::streamingCompletion(const std::string &prompt,
    std::function<void(const std::string &)> callback)
{
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

    for(int i=0; i<n_len; ++i)
    {
        auto *logits=llama_get_logits_ith(m_ctx, batch.n_tokens-1);

        candidates.clear();
        for(llama_token token_id=0; token_id<n_vocab; token_id++)
        {
            candidates.push_back({ token_id, logits[token_id], 0.0f });
        }

        auto max_it=std::max_element(candidates.begin(), candidates.end(),
            [](const llama_token_data &a, const llama_token_data &b)
            {
                return a.logit<b.logit;
            });
        const llama_token next_token=max_it->id;

        if(next_token==llama_token_eos(llama_model_get_vocab(m_model)))
        {
            break;
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
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
    }

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

bool LlamaInterface::loadModel(const std::string &modelName)
{
    if(m_modelInfo&&m_modelInfo->model==modelName)
    {
        return true;
    }

    auto modelInfo = ModelManager::instance().getModelInfo(modelName);
    if (!modelInfo) {
        spdlog::error("Llama model not found in ModelManager: {}", modelName);
        return false;
    }

    if (!modelInfo->filePath) {
        auto it = std::find_if(m_llamaModels.begin(), m_llamaModels.end(),
            [&](const LlamaModelInfo &info)
            {
                return info.modelInfo.model==modelName;
            });

        if(it==m_llamaModels.end())
        {
            spdlog::error("Llama model not found in llama config: {}", modelName);
            return false;
        }
        
        if(it->downloadStatus==DownloadStatus::InProgress)
        {
            return false;
        }

        if(!isModelDownloaded(*it))
        {
            downloadModel(*it);
            return false;
        }
        modelInfo->filePath = it->filePath;
    }

    m_modelInfo = modelInfo;

    auto mparams=llama_model_default_params();
    const char *modelPath=modelInfo->filePath->c_str();
    m_model=llama_model_load_from_file(modelPath, mparams);
    if(!m_model)
    {
        spdlog::error("Failed to load model: {}", modelPath);
        return false;
    }

    auto cparams=llama_context_default_params();
    cparams.n_ctx=m_modelInfo->contextWindow;
    cparams.n_threads=std::thread::hardware_concurrency();
    cparams.n_threads_batch=std::thread::hardware_concurrency();

    m_ctx=llama_init_from_model(m_model, cparams);

    if(!m_ctx)
    {
        spdlog::error("Failed to create context for model: {}", m_modelInfo->model);
    }
    return m_ctx!=nullptr;
}


} // namespace hermesaxiom