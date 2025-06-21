#include "hermesaxiom/providers/llama_llm.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace hermesaxiom
{

LlamaLLM::LlamaLLM(const ModelInfo &modelInfo) :
    m_modelInfo(modelInfo)
{
    loadModel();
}

LlamaLLM::~LlamaLLM()
{
    if (m_ctx)
    {
        llama_free(m_ctx);
    }
    if (m_model)
    {
        llama_model_free(m_model);
    }
}

void LlamaLLM::loadModel()
{
    auto mparams = llama_model_default_params();
    m_model = llama_model_load_from_file(m_modelInfo.model.c_str(), mparams);
    if (!m_model)
    {
        spdlog::error("Failed to load model: {}", m_modelInfo.model);
        return;
    }

    auto cparams = llama_context_default_params();
    cparams.n_ctx = m_modelInfo.contextWindow;
    cparams.n_threads = std::thread::hardware_concurrency();

    m_ctx = llama_init_from_model(m_model, cparams);
    if (!m_ctx)
    {
        spdlog::error("Failed to create context for model: {}", m_modelInfo.model);
    }
}

ErrorCode LlamaLLM::completion(const CompletionRequest &request,
                               CompletionResponse &response)
{
    if (!m_ctx)
    {
        return ErrorCode::ModelNotLoaded;
    }

    std::string prompt_str;
    for (const auto& msg : request.messages)
    {
        prompt_str += msg.content;
    }

    // tokenize the prompt
    std::vector<llama_token> tokens_list(prompt_str.size());
    int n_tokens = llama_tokenize(llama_model_get_vocab(m_model), prompt_str.c_str(), prompt_str.length(), tokens_list.data(), tokens_list.size(), true, false);
    if (n_tokens < 0) {
        spdlog::error("Failed to tokenize prompt");
        return ErrorCode::GenerationError;
    }
    tokens_list.resize(n_tokens);

    llama_batch batch = llama_batch_init(512, 0, 1);

    batch.n_tokens = n_tokens;
    for (int32_t i = 0; i < batch.n_tokens; i++) {
        batch.token[i]  = tokens_list[i];
        batch.pos[i]    = i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i] = 0;
    }
    batch.logits[batch.n_tokens - 1] = 1;

    if (llama_decode(m_ctx, batch) != 0) {
        spdlog::error("llama_decode failed");
        llama_batch_free(batch);
        return ErrorCode::GenerationError;
    }

    std::string result_text;
    int n_cur = n_tokens;
    int n_len = m_modelInfo.maxOutputTokens;
    auto n_vocab = llama_vocab_n_tokens(llama_model_get_vocab(m_model));
    std::vector<llama_token_data> candidates;
    candidates.reserve(n_vocab);

    for (int i = 0; i < n_len; ++i) {
        auto* logits = llama_get_logits_ith(m_ctx, batch.n_tokens - 1);

        candidates.clear();
        for (llama_token token_id = 0; token_id < n_vocab; token_id++) {
            candidates.push_back({token_id, logits[token_id], 0.0f});
        }
        
        auto max_it = std::max_element(candidates.begin(), candidates.end(),
            [](const llama_token_data& a, const llama_token_data& b) {
                return a.logit < b.logit;
            });
        const llama_token next_token = max_it->id;

        if (next_token == llama_token_eos(llama_model_get_vocab(m_model))) {
            break;
        }

        char piece[16];
        int len = llama_token_to_piece(llama_model_get_vocab(m_model), next_token, piece, sizeof(piece), 0, false);
        if (len > 0) {
            result_text.append(piece, len);
        }

        batch.n_tokens = 1;
        batch.token[0] = next_token;
        batch.pos[0] = n_cur;
        batch.logits[0] = 1;

        n_cur++;

        if (llama_decode(m_ctx, batch) != 0) {
            spdlog::error("llama_decode failed");
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
    }

    llama_batch_free(batch);

    response.text = result_text;
    response.provider = "llama";
    response.model = m_modelInfo.model;

    return ErrorCode::Success;
}

ErrorCode LlamaLLM::streamingCompletion(const CompletionRequest &request,
                                        std::function<void(const std::string&)> callback)
{
    if (!m_ctx)
    {
        return ErrorCode::ModelNotLoaded;
    }

    std::string prompt_str;
    for (const auto& msg : request.messages)
    {
        prompt_str += msg.content;
    }

    // tokenize the prompt
    std::vector<llama_token> tokens_list(prompt_str.size());
    int n_tokens = llama_tokenize(llama_model_get_vocab(m_model), prompt_str.c_str(), prompt_str.length(), tokens_list.data(), tokens_list.size(), true, false);
    if (n_tokens < 0) {
        spdlog::error("Failed to tokenize prompt");
        return ErrorCode::GenerationError;
    }
    tokens_list.resize(n_tokens);

    llama_batch batch = llama_batch_init(512, 0, 1);

    batch.n_tokens = n_tokens;
    for (int32_t i = 0; i < batch.n_tokens; i++) {
        batch.token[i]  = tokens_list[i];
        batch.pos[i]    = i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i] = 0;
    }
    batch.logits[batch.n_tokens - 1] = 1;

    if (llama_decode(m_ctx, batch) != 0) {
        spdlog::error("llama_decode failed");
        llama_batch_free(batch);
        return ErrorCode::GenerationError;
    }

    int n_cur = n_tokens;
    int n_len = m_modelInfo.maxOutputTokens;
    auto n_vocab = llama_vocab_n_tokens(llama_model_get_vocab(m_model));
    std::vector<llama_token_data> candidates;
    candidates.reserve(n_vocab);

    for (int i = 0; i < n_len; ++i) {
        auto* logits = llama_get_logits_ith(m_ctx, batch.n_tokens - 1);

        candidates.clear();
        for (llama_token token_id = 0; token_id < n_vocab; token_id++) {
            candidates.push_back({token_id, logits[token_id], 0.0f});
        }
        
        auto max_it = std::max_element(candidates.begin(), candidates.end(),
            [](const llama_token_data& a, const llama_token_data& b) {
                return a.logit < b.logit;
            });
        const llama_token next_token = max_it->id;

        if (next_token == llama_token_eos(llama_model_get_vocab(m_model)))
        {
            break;
        }
char piece[16];
int len = llama_token_to_piece(llama_model_get_vocab(m_model), next_token, piece, sizeof(piece), 0, false);
        if (len > 0) {
            callback(std::string(piece, len));
        }

        batch.n_tokens = 1;
        batch.token[0] = next_token;
        batch.pos[0] = n_cur;
        batch.logits[0] = 1;

        n_cur++;

        if (llama_decode(m_ctx, batch) != 0) {
            spdlog::error("llama_decode failed");
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
    }

    llama_batch_free(batch);

    return ErrorCode::Success;
}

} // namespace hermesaxiom