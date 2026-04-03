#include "arbiterAI/providers/llama.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/telemetryCollector.h"

#include <llama.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>
#include <variant>

namespace arbiterAI
{

Llama::Llama():
    BaseProvider("llama")
{
}

Llama::~Llama()
{
}

ErrorCode Llama::completion(const CompletionRequest &request,
    const ModelInfo &model,
    CompletionResponse &response)
{
    ModelRuntime &runtime=ModelRuntime::instance();

    // Ensure model is loaded
    ErrorCode loadResult=runtime.loadModel(request.model);
    if(loadResult!=ErrorCode::Success)
    {
        return loadResult;
    }

    llama_model *llamaModel=runtime.getLlamaModel(request.model);
    llama_context *llamaCtx=runtime.getLlamaContext(request.model);

    if(!llamaModel||!llamaCtx)
    {
        spdlog::error("Llama model handles not available for: {}", request.model);
        return ErrorCode::ModelNotLoaded;
    }

    runtime.beginInference(request.model);

    std::chrono::steady_clock::time_point startTime=std::chrono::steady_clock::now();

    std::string resultText;
    int promptTokens=0;
    int completionTokens=0;
    double promptTimeMs=0.0;
    double generationTimeMs=0.0;

    ErrorCode code=runInference(llamaModel, llamaCtx, request, model,
        resultText, promptTokens, completionTokens, promptTimeMs, generationTimeMs, nullptr);

    std::chrono::steady_clock::time_point endTime=std::chrono::steady_clock::now();
    double totalTimeMs=std::chrono::duration<double, std::milli>(endTime-startTime).count();

    runtime.endInference();

    if(code==ErrorCode::Success)
    {
        response.text=resultText;
        response.provider="llama";
        response.model=request.model;
        response.usage.prompt_tokens=promptTokens;
        response.usage.completion_tokens=completionTokens;
        response.usage.total_tokens=promptTokens+completionTokens;
        response.finishReason="stop";

        // Record telemetry
        std::optional<LoadedModel> state=runtime.getModelState(request.model);

        InferenceStats stats;
        stats.model=request.model;
        stats.variant=state?state->variant:"";
        stats.promptTokens=promptTokens;
        stats.completionTokens=completionTokens;
        stats.totalTimeMs=totalTimeMs;
        stats.promptTimeMs=promptTimeMs;
        stats.generationTimeMs=generationTimeMs;
        stats.tokensPerSecond=totalTimeMs>0.0?(completionTokens/(totalTimeMs/1000.0)):0.0;
        stats.promptTokensPerSecond=promptTimeMs>0.0?(promptTokens/(promptTimeMs/1000.0)):0.0;
        stats.generationTokensPerSecond=generationTimeMs>0.0?(completionTokens/(generationTimeMs/1000.0)):0.0;
        stats.timestamp=std::chrono::system_clock::now();
        TelemetryCollector::instance().recordInference(stats);
    }

    return code;
}

ErrorCode Llama::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    ModelRuntime &runtime=ModelRuntime::instance();

    ErrorCode loadResult=runtime.loadModel(request.model);
    if(loadResult!=ErrorCode::Success)
    {
        return loadResult;
    }

    llama_model *llamaModel=runtime.getLlamaModel(request.model);
    llama_context *llamaCtx=runtime.getLlamaContext(request.model);

    if(!llamaModel||!llamaCtx)
    {
        spdlog::error("Llama model handles not available for: {}", request.model);
        return ErrorCode::ModelNotLoaded;
    }

    std::optional<ModelInfo> modelInfo=runtime.getLoadedModelInfo(request.model);
    if(!modelInfo)
    {
        return ErrorCode::ModelNotFound;
    }

    runtime.beginInference(request.model);

    std::chrono::steady_clock::time_point startTime=std::chrono::steady_clock::now();

    std::string resultText;
    int promptTokens=0;
    int completionTokens=0;
    double promptTimeMs=0.0;
    double generationTimeMs=0.0;

    ErrorCode code=runInference(llamaModel, llamaCtx, request, *modelInfo,
        resultText, promptTokens, completionTokens, promptTimeMs, generationTimeMs, callback);

    std::chrono::steady_clock::time_point endTime=std::chrono::steady_clock::now();
    double totalTimeMs=std::chrono::duration<double, std::milli>(endTime-startTime).count();

    runtime.endInference();

    if(code==ErrorCode::Success)
    {
        std::optional<LoadedModel> state=runtime.getModelState(request.model);

        InferenceStats stats;
        stats.model=request.model;
        stats.variant=state?state->variant:"";
        stats.promptTokens=promptTokens;
        stats.completionTokens=completionTokens;
        stats.totalTimeMs=totalTimeMs;
        stats.promptTimeMs=promptTimeMs;
        stats.generationTimeMs=generationTimeMs;
        stats.tokensPerSecond=totalTimeMs>0.0?(completionTokens/(totalTimeMs/1000.0)):0.0;
        stats.promptTokensPerSecond=promptTimeMs>0.0?(promptTokens/(promptTimeMs/1000.0)):0.0;
        stats.generationTokensPerSecond=generationTimeMs>0.0?(completionTokens/(generationTimeMs/1000.0)):0.0;
        stats.timestamp=std::chrono::system_clock::now();
        TelemetryCollector::instance().recordInference(stats);
    }

    return code;
}

ErrorCode Llama::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    ModelRuntime &runtime=ModelRuntime::instance();

    ErrorCode loadResult=runtime.loadModel(request.model);
    if(loadResult!=ErrorCode::Success)
    {
        return loadResult;
    }

    llama_model *llamaModel=runtime.getLlamaModel(request.model);
    llama_context *llamaCtx=runtime.getLlamaContext(request.model);

    if(!llamaModel||!llamaCtx)
    {
        return ErrorCode::ModelNotLoaded;
    }

    // Combine input text
    std::string inputText;
    std::visit([&inputText](auto &&arg)
        {
            using T=std::decay_t<decltype(arg)>;
            if constexpr(std::is_same_v<T, std::string>)
            {
                inputText=arg;
            }
            else
            {
                for(const std::string &s:arg)
                {
                    inputText+=s;
                }
            }
        }, request.input);

    const llama_vocab *vocab=llama_model_get_vocab(llamaModel);

    // Tokenize
    std::vector<llama_token> tokens(inputText.size()+16);
    int nTokens=llama_tokenize(vocab, inputText.c_str(), inputText.length(),
        tokens.data(), tokens.size(), true, false);
    if(nTokens<0)
    {
        spdlog::error("Failed to tokenize embedding input");
        return ErrorCode::GenerationError;
    }
    tokens.resize(nTokens);

    llama_batch batch=llama_batch_init(nTokens, 0, 1);

    batch.n_tokens=nTokens;
    for(int32_t i=0; i<batch.n_tokens; i++)
    {
        batch.token[i]=tokens[i];
        batch.pos[i]=i;
        batch.n_seq_id[i]=1;
        batch.seq_id[i][0]=0;
        batch.logits[i]=0;
    }
    batch.logits[batch.n_tokens-1]=1;

    if(llama_decode(llamaCtx, batch)!=0)
    {
        spdlog::error("llama_decode failed for embeddings");
        llama_batch_free(batch);
        return ErrorCode::GenerationError;
    }

    const float *embeddingsPtr=llama_get_embeddings(llamaCtx);
    if(!embeddingsPtr)
    {
        spdlog::error("llama_get_embeddings returned null");
        llama_batch_free(batch);
        return ErrorCode::GenerationError;
    }

    int nEmbd=llama_model_n_embd(llamaModel);

    response.model=request.model;
    response.usage.prompt_tokens=nTokens;
    response.usage.total_tokens=nTokens;

    Embedding emb;
    emb.embedding.assign(embeddingsPtr, embeddingsPtr+nEmbd);
    response.data.push_back(emb);

    llama_batch_free(batch);
    return ErrorCode::Success;
}

DownloadStatus Llama::getDownloadStatus(const std::string &modelName, std::string &error)
{
    std::optional<LoadedModel> state=ModelRuntime::instance().getModelState(modelName);
    if(!state)
    {
        return DownloadStatus::NotStarted;
    }

    if(state->state==ModelState::Downloading)
    {
        return DownloadStatus::InProgress;
    }
    if(state->state==ModelState::Loaded||state->state==ModelState::Ready)
    {
        return DownloadStatus::Completed;
    }
    return DownloadStatus::NotStarted;
}

ErrorCode Llama::getAvailableModels(std::vector<std::string> &models)
{
    std::vector<ModelInfo> allModels=ModelManager::instance().getModelsByRanking();
    for(const ModelInfo &info:allModels)
    {
        if(info.provider=="llama")
        {
            models.push_back(info.model);
        }
    }
    return ErrorCode::Success;
}

std::string Llama::applyTemplate(llama_model *model,
    const std::vector<Message> &messages) const
{
    // Build llama_chat_message array
    std::vector<llama_chat_message> chatMessages;
    chatMessages.reserve(messages.size());
    for(const Message &msg:messages)
    {
        chatMessages.push_back({msg.role.c_str(), msg.content.c_str()});
    }

    // Get the chat template from the model metadata
    const char *tmpl=llama_model_chat_template(model, nullptr);

    if(!tmpl)
    {
        spdlog::warn("Chat template not found in model metadata, using ChatML fallback");
        std::string result;
        for(const Message &msg:messages)
        {
            result+="<|im_start|>"+msg.role+"\n"+msg.content+"<|im_end|>\n";
        }
        result+="<|im_start|>assistant\n";
        return result;
    }

    // First call to get required buffer size
    int32_t len=llama_chat_apply_template(
        tmpl,
        chatMessages.data(), chatMessages.size(),
        true,
        nullptr, 0);

    if(len<0)
    {
        spdlog::warn("llama_chat_apply_template failed, using ChatML fallback");
        std::string result;
        for(const Message &msg:messages)
        {
            result+="<|im_start|>"+msg.role+"\n"+msg.content+"<|im_end|>\n";
        }
        result+="<|im_start|>assistant\n";
        return result;
    }

    std::string result(len+1, '\0');

    llama_chat_apply_template(
        tmpl,
        chatMessages.data(), chatMessages.size(),
        true,
        result.data(), result.size());

    result.resize(len);
    return result;
}

ErrorCode Llama::runInference(llama_model *model, llama_context *ctx,
    const CompletionRequest &request, const ModelInfo &modelInfo,
    std::string &result, int &promptTokens, int &completionTokens,
    double &promptTimeMs, double &generationTimeMs,
    std::function<void(const std::string &)> streamCallback)
{
    const llama_vocab *vocab=llama_model_get_vocab(model);

    // Apply chat template to format messages properly
    std::string prompt=applyTemplate(model, request.messages);

    // Tokenize the formatted prompt
    std::vector<llama_token> tokensList(prompt.size()+256);
    int nTokens=llama_tokenize(vocab, prompt.c_str(), prompt.length(),
        tokensList.data(), tokensList.size(), true, false);
    if(nTokens<0)
    {
        // Buffer too small, resize and retry
        tokensList.resize(-nTokens);
        nTokens=llama_tokenize(vocab, prompt.c_str(), prompt.length(),
            tokensList.data(), tokensList.size(), true, false);
        if(nTokens<0)
        {
            spdlog::error("Failed to tokenize prompt");
            return ErrorCode::GenerationError;
        }
    }
    tokensList.resize(nTokens);
    promptTokens=nTokens;

    // Clear KV cache for fresh inference
    llama_memory_clear(llama_get_memory(ctx), true);

    llama_batch batch=llama_batch_init(std::max(nTokens, 512), 0, 1);

    // Fill batch with prompt tokens
    batch.n_tokens=nTokens;
    for(int32_t i=0; i<batch.n_tokens; i++)
    {
        batch.token[i]=tokensList[i];
        batch.pos[i]=i;
        batch.n_seq_id[i]=1;
        batch.seq_id[i][0]=0;
        batch.logits[i]=0;
    }
    batch.logits[batch.n_tokens-1]=1;

    // Process prompt (timed)
    std::chrono::steady_clock::time_point promptStart=std::chrono::steady_clock::now();

    if(llama_decode(ctx, batch)!=0)
    {
        spdlog::error("llama_decode failed during prompt processing");
        llama_batch_free(batch);
        return ErrorCode::GenerationError;
    }

    std::chrono::steady_clock::time_point promptEnd=std::chrono::steady_clock::now();
    promptTimeMs=std::chrono::duration<double, std::milli>(promptEnd-promptStart).count();

    int maxOutputTokens=request.max_tokens.value_or(modelInfo.maxOutputTokens);
    int nCur=nTokens;
    completionTokens=0;

    // Set up sampler chain
    llama_sampler_chain_params samplerParams=llama_sampler_chain_default_params();
    llama_sampler *samplerChain=llama_sampler_chain_init(samplerParams);

    llama_sampler_chain_add(samplerChain, llama_sampler_init_penalties(
        -1,
        1.0f,
        request.frequency_penalty.value_or(0.0f),
        request.presence_penalty.value_or(0.0f)));

    if(request.top_p.has_value())
    {
        llama_sampler_chain_add(samplerChain, llama_sampler_init_top_p(*request.top_p, 1));
    }
    if(request.temperature.has_value()&&*request.temperature>0.0)
    {
        llama_sampler_chain_add(samplerChain, llama_sampler_init_temp(*request.temperature));
    }
    llama_sampler_chain_add(samplerChain, llama_sampler_init_greedy());

    // Accept prompt tokens into sampler
    for(const llama_token &token:tokensList)
    {
        llama_sampler_accept(samplerChain, token);
    }

    // Generation loop (timed)
    std::chrono::steady_clock::time_point genStart=std::chrono::steady_clock::now();

    for(int i=0; i<maxOutputTokens; ++i)
    {
        llama_token nextToken=llama_sampler_sample(samplerChain, ctx, -1);
        llama_sampler_accept(samplerChain, nextToken);

        // Check for end of sequence
        if(llama_vocab_is_eog(vocab, nextToken))
        {
            break;
        }

        // Convert token to text
        char piece[64];
        int len=llama_token_to_piece(vocab, nextToken, piece, sizeof(piece), 0, false);
        if(len>0)
        {
            std::string tokenText(piece, len);
            result+=tokenText;
            completionTokens++;

            if(streamCallback)
            {
                streamCallback(tokenText);
            }
        }

        // Check stop sequences
        if(request.stop.has_value())
        {
            bool stopFound=false;
            for(const std::string &stopWord:*request.stop)
            {
                if(result.size()>=stopWord.size()&&
                    result.substr(result.size()-stopWord.size())==stopWord)
                {
                    // Remove the stop sequence from result
                    result.resize(result.size()-stopWord.size());
                    stopFound=true;
                    break;
                }
            }
            if(stopFound)
            {
                break;
            }
        }

        // Prepare next batch
        batch.n_tokens=1;
        batch.token[0]=nextToken;
        batch.pos[0]=nCur;
        batch.n_seq_id[0]=1;
        batch.seq_id[0][0]=0;
        batch.logits[0]=1;
        nCur++;

        if(llama_decode(ctx, batch)!=0)
        {
            spdlog::error("llama_decode failed during generation");
            llama_sampler_free(samplerChain);
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
    }

    std::chrono::steady_clock::time_point genEnd=std::chrono::steady_clock::now();
    generationTimeMs=std::chrono::duration<double, std::milli>(genEnd-genStart).count();

    llama_sampler_free(samplerChain);
    llama_batch_free(batch);

    return ErrorCode::Success;
}

} // namespace arbiterAI
