#include "arbiterAI/providers/llama.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/telemetryCollector.h"

#include <llama.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <algorithm>
#include <mutex>
#include <thread>
#include <variant>
#include <sstream>

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

    spdlog::info("[llama] completion request for model '{}', waiting for inference lock", request.model);
    auto lockWaitStart=std::chrono::steady_clock::now();

    runtime.beginInference(request.model);

    // Use timed lock to avoid blocking HTTP threads indefinitely.
    // If the lock can't be acquired within 5 minutes, return overloaded.
    bool lockAcquired=runtime.getInferenceMutex().try_lock_for(std::chrono::minutes(5));
    if(!lockAcquired)
    {
        runtime.endInference(request.model);
        spdlog::warn("[llama] completion request for model '{}' timed out waiting for inference lock", request.model);
        return ErrorCode::ServerOverloaded;
    }
    std::lock_guard<std::timed_mutex> inferenceLock(runtime.getInferenceMutex(), std::adopt_lock);

    auto lockWaitMs=std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now()-lockWaitStart).count();
    if(lockWaitMs>100.0)
    {
        spdlog::warn("[llama] inference lock acquired after {:.1f}ms wait", lockWaitMs);
    }

    spdlog::info("[llama] starting inference for model '{}'", request.model);
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

    runtime.endInference(request.model);

    if(code!=ErrorCode::Success)
    {
        spdlog::error("[llama] inference failed for model '{}' after {:.1f}ms (error={})",
            request.model, totalTimeMs, static_cast<int>(code));
        return code;
    }

    spdlog::info("[llama] inference complete: prompt={} tokens ({:.1f}ms), gen={} tokens ({:.1f}ms), total={:.1f}ms",
        promptTokens, promptTimeMs, completionTokens, generationTimeMs, totalTimeMs);

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

    spdlog::info("[llama] streaming completion request for model '{}', waiting for inference lock", request.model);
    auto lockWaitStart=std::chrono::steady_clock::now();

    runtime.beginInference(request.model);

    bool lockAcquired=runtime.getInferenceMutex().try_lock_for(std::chrono::minutes(5));
    if(!lockAcquired)
    {
        runtime.endInference(request.model);
        spdlog::warn("[llama] streaming completion request for model '{}' timed out waiting for inference lock", request.model);
        return ErrorCode::ServerOverloaded;
    }
    std::lock_guard<std::timed_mutex> inferenceLock(runtime.getInferenceMutex(), std::adopt_lock);

    auto lockWaitMs=std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now()-lockWaitStart).count();
    if(lockWaitMs>100.0)
    {
        spdlog::warn("[llama] streaming inference lock acquired after {:.1f}ms wait", lockWaitMs);
    }

    spdlog::info("[llama] starting streaming inference for model '{}'", request.model);
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

    runtime.endInference(request.model);

    if(code!=ErrorCode::Success)
    {
        spdlog::error("[llama] streaming inference failed for model '{}' after {:.1f}ms (error={})",
            request.model, totalTimeMs, static_cast<int>(code));
        return code;
    }

    spdlog::info("[llama] streaming inference complete: prompt={} tokens ({:.1f}ms), gen={} tokens ({:.1f}ms), total={:.1f}ms",
        promptTokens, promptTimeMs, completionTokens, generationTimeMs, totalTimeMs);

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

    std::lock_guard<std::timed_mutex> inferenceLock(runtime.getInferenceMutex());

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

    int nBatch=static_cast<int>(llama_n_batch(llamaCtx));
    llama_batch batch=llama_batch_init(std::max(nBatch, 512), 0, 1);

    for(int start=0; start<nTokens; start+=nBatch)
    {
        int chunkSize=std::min(nBatch, nTokens-start);
        bool isLastChunk=(start+chunkSize>=nTokens);

        batch.n_tokens=chunkSize;
        for(int32_t i=0; i<chunkSize; i++)
        {
            batch.token[i]=tokens[start+i];
            batch.pos[i]=start+i;
            batch.n_seq_id[i]=1;
            batch.seq_id[i][0]=0;
            batch.logits[i]=0;
        }
        if(isLastChunk)
        {
            batch.logits[chunkSize-1]=1;
        }

        if(llama_decode(llamaCtx, batch)!=0)
        {
            spdlog::error("llama_decode failed for embeddings (chunk at offset {})", start);
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
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

std::string Llama::formatHarmonyPrompt(const CompletionRequest &request,
    const ModelInfo &modelInfo) const
{
    std::string prompt;
    bool hasTools=request.tools.has_value()&&!request.tools->empty();

    // Build system message
    std::string systemContent="You are ChatGPT, a large language model trained by OpenAI.\n"
        "Knowledge cutoff: 2024-06\n"
        "Current date: 2025-06-28\n"
        "\n"
        "Reasoning: high\n"
        "\n"
        "# Valid channels: analysis, commentary, final. Channel must be included for every message.";

    if(hasTools)
    {
        systemContent+="\nCalls to these tools must go to the commentary channel: 'functions'.";
    }

    prompt+="<|start|>system<|message|>"+systemContent+"<|end|>";

    // Build developer message from the first system-role message (if any)
    // and tool definitions
    std::string developerContent;
    bool hasInstructions=false;

    for(const Message &msg:request.messages)
    {
        if(msg.role=="system")
        {
            if(!developerContent.empty())
                developerContent+="\n\n";
            developerContent+="# Instructions\n\n"+msg.content;
            hasInstructions=true;
        }
    }

    if(hasTools)
    {
        if(!developerContent.empty())
            developerContent+="\n\n";
        else
            developerContent+="# Instructions\n\nYou are a helpful assistant.\n\n";

        developerContent+="# Tools\n\n## functions\n\nnamespace functions {\n";

        for(const ToolDefinition &tool:*request.tools)
        {
            if(!tool.description.empty())
                developerContent+="\n// "+tool.description+"\n";
            else
                developerContent+="\n";

            if(tool.parametersSchema.is_object()&&tool.parametersSchema.contains("properties")
                &&!tool.parametersSchema["properties"].empty())
            {
                developerContent+="type "+tool.name+" = (_: {\n";

                const nlohmann::json &props=tool.parametersSchema["properties"];
                std::vector<std::string> required;
                if(tool.parametersSchema.contains("required")&&tool.parametersSchema["required"].is_array())
                {
                    for(const nlohmann::json &r:tool.parametersSchema["required"])
                    {
                        if(r.is_string())
                            required.push_back(r.get<std::string>());
                    }
                }

                for(auto it=props.begin(); it!=props.end(); ++it)
                {
                    std::string paramName=it.key();
                    const nlohmann::json &paramDef=it.value();

                    // Add description as comment
                    if(paramDef.contains("description"))
                        developerContent+="// "+paramDef["description"].get<std::string>()+"\n";

                    bool isRequired=std::find(required.begin(), required.end(), paramName)!=required.end();

                    // Determine type string
                    std::string typeStr="any";
                    if(paramDef.contains("type"))
                    {
                        std::string jsonType=paramDef["type"].get<std::string>();
                        if(jsonType=="string")
                        {
                            if(paramDef.contains("enum"))
                            {
                                typeStr="";
                                for(size_t i=0; i<paramDef["enum"].size(); i++)
                                {
                                    if(i>0) typeStr+=" | ";
                                    typeStr+="\""+paramDef["enum"][i].get<std::string>()+"\"";
                                }
                            }
                            else
                            {
                                typeStr="string";
                            }
                        }
                        else if(jsonType=="integer"||jsonType=="number")
                            typeStr="number";
                        else if(jsonType=="boolean")
                            typeStr="boolean";
                        else if(jsonType=="array")
                        {
                            if(paramDef.contains("items")&&paramDef["items"].contains("type"))
                            {
                                std::string itemType=paramDef["items"]["type"].get<std::string>();
                                if(itemType=="string") typeStr="string[]";
                                else if(itemType=="number"||itemType=="integer") typeStr="number[]";
                                else typeStr="any[]";
                            }
                            else
                            {
                                typeStr="any[]";
                            }
                        }
                    }

                    developerContent+=paramName+(isRequired?": ":"?: ")+typeStr+",";

                    // Add default as inline comment
                    if(paramDef.contains("default"))
                    {
                        developerContent+=" // default: "+paramDef["default"].dump();
                    }
                    developerContent+="\n";
                }

                developerContent+="}) => any;\n";
            }
            else
            {
                developerContent+="type "+tool.name+" = () => any;\n";
            }
        }

        developerContent+="\n} // namespace functions";
    }

    if(!developerContent.empty())
    {
        prompt+="<|start|>developer<|message|>"+developerContent+"<|end|>";
    }

    // Format conversation messages (skip system messages, already handled above)
    for(const Message &msg:request.messages)
    {
        if(msg.role=="system")
            continue;

        if(msg.role=="user")
        {
            prompt+="<|start|>user<|message|>"+msg.content+"<|end|>";
        }
        else if(msg.role=="assistant")
        {
            if(msg.toolCalls.has_value()&&!msg.toolCalls->empty())
            {
                // Assistant message with tool calls — recreate harmony format
                // First the content/reasoning if any
                if(!msg.content.empty())
                {
                    prompt+="<|start|>assistant<|channel|>final<|message|>"+msg.content+"<|end|>";
                }

                // Then each tool call
                for(const ToolCall &tc:*msg.toolCalls)
                {
                    std::string argsStr;
                    if(tc.arguments.is_string())
                        argsStr=tc.arguments.get<std::string>();
                    else
                        argsStr=tc.arguments.dump();

                    prompt+="<|start|>assistant<|channel|>commentary to=functions."+tc.name
                        +" <|constrain|>json<|message|>"+argsStr+"<|call|>";
                }
            }
            else
            {
                // Regular assistant message — use final channel
                prompt+="<|start|>assistant<|channel|>final<|message|>"+msg.content+"<|end|>";
            }
        }
        else if(msg.role=="tool")
        {
            // Tool result message
            std::string toolName=msg.name.value_or("unknown");
            prompt+="<|start|>functions."+toolName+" to=assistant<|channel|>commentary<|message|>"
                +msg.content+"<|end|>";
        }
    }

    // Prompt the assistant to start generating
    prompt+="<|start|>assistant";

    return prompt;
}

ErrorCode Llama::runInference(llama_model *model, llama_context *ctx,
    const CompletionRequest &request, const ModelInfo &modelInfo,
    std::string &result, int &promptTokens, int &completionTokens,
    double &promptTimeMs, double &generationTimeMs,
    std::function<void(const std::string &)> streamCallback)
{
    const llama_vocab *vocab=llama_model_get_vocab(model);
    bool harmonyMode=(modelInfo.apiFormat=="harmony");

    // Apply chat template to format messages properly
    std::string prompt;
    if(harmonyMode)
    {
        prompt=formatHarmonyPrompt(request, modelInfo);
    }
    else
    {
        prompt=applyTemplate(model, request.messages);
    }

    // Tokenize the formatted prompt — use special token parsing for harmony
    std::vector<llama_token> tokensList(prompt.size()+256);
    int nTokens=llama_tokenize(vocab, prompt.c_str(), prompt.length(),
        tokensList.data(), tokensList.size(), true, harmonyMode);
    if(nTokens<0)
    {
        // Buffer too small, resize and retry
        tokensList.resize(-nTokens);
        nTokens=llama_tokenize(vocab, prompt.c_str(), prompt.length(),
            tokensList.data(), tokensList.size(), true, harmonyMode);
        if(nTokens<0)
        {
            spdlog::error("Failed to tokenize prompt");
            return ErrorCode::GenerationError;
        }
    }
    tokensList.resize(nTokens);
    promptTokens=nTokens;

    // Clear KV cache for fresh inference
    spdlog::debug("[llama] clearing KV cache, prompt tokens={}", nTokens);
    llama_memory_clear(llama_get_memory(ctx), true);

    int nBatch=static_cast<int>(llama_n_batch(ctx));
    llama_batch batch=llama_batch_init(std::max(nBatch, 512), 0, 1);

    // Process prompt (timed) — chunk into n_batch-sized pieces
    std::chrono::steady_clock::time_point promptStart=std::chrono::steady_clock::now();

    for(int start=0; start<nTokens; start+=nBatch)
    {
        int chunkSize=std::min(nBatch, nTokens-start);
        bool isLastChunk=(start+chunkSize>=nTokens);

        batch.n_tokens=chunkSize;
        for(int32_t i=0; i<chunkSize; i++)
        {
            batch.token[i]=tokensList[start+i];
            batch.pos[i]=start+i;
            batch.n_seq_id[i]=1;
            batch.seq_id[i][0]=0;
            batch.logits[i]=0;
        }
        // Only request logits for the very last token of the prompt
        if(isLastChunk)
        {
            batch.logits[chunkSize-1]=1;
        }

        int decodeResult=llama_decode(ctx, batch);
        if(decodeResult!=0)
        {
            spdlog::error("[llama] llama_decode failed during prompt processing (chunk at offset {}, chunkSize={}, totalTokens={}, result={})",
                start, chunkSize, nTokens, decodeResult);
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
    }

    std::chrono::steady_clock::time_point promptEnd=std::chrono::steady_clock::now();
    promptTimeMs=std::chrono::duration<double, std::milli>(promptEnd-promptStart).count();

    int maxOutputTokens=request.max_tokens.value_or(modelInfo.maxOutputTokens);

    // Harmony mode models use internal reasoning tokens that are hidden from the client.
    // The client's max_tokens should apply to visible output, so we need extra headroom
    // for the analysis channel. Apply a multiplier to ensure the model can complete both
    // reasoning and the final response.
    if(harmonyMode)
    {
        int minHarmonyTokens=std::max(maxOutputTokens*8, 16384);
        maxOutputTokens=std::min(minHarmonyTokens, modelInfo.maxOutputTokens>0?modelInfo.maxOutputTokens:131072);
    }

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

    // For harmony mode, look up special stop token IDs
    llama_token harmonyCallToken=-1;
    llama_token harmonyReturnToken=-1;
    if(harmonyMode)
    {
        // Try to find <|call|> and <|return|> tokens by tokenizing them
        llama_token buf[4];
        int n;

        n=llama_tokenize(vocab, "<|call|>", 8, buf, 4, false, true);
        if(n==1) harmonyCallToken=buf[0];

        n=llama_tokenize(vocab, "<|return|>", 10, buf, 4, false, true);
        if(n==1) harmonyReturnToken=buf[0];

        spdlog::debug("[llama] harmony stop tokens: <|call|>={}, <|return|>={}",
            harmonyCallToken, harmonyReturnToken);
    }

    for(int i=0; i<maxOutputTokens; ++i)
    {
        llama_token nextToken=llama_sampler_sample(samplerChain, ctx, -1);
        llama_sampler_accept(samplerChain, nextToken);

        // Check harmony-specific stop tokens BEFORE generic EOG check,
        // because <|return|> and <|call|> are marked as EOG in the vocab
        // but we need to handle them specially in harmony mode.
        if(harmonyMode)
        {
            if(nextToken==harmonyCallToken||nextToken==harmonyReturnToken)
            {
                // Append the special token text so the output parser can detect it
                if(nextToken==harmonyCallToken)
                {
                    result+="<|call|>";
                    if(streamCallback) streamCallback("<|call|>");
                }
                completionTokens++;
                break;
            }
        }

        // Check for end of sequence (skip harmony-handled tokens)
        if(llama_vocab_is_eog(vocab, nextToken))
        {
            if(harmonyMode)
            {
                spdlog::info("[llama] harmony EOG hit: token={}", nextToken);
            }
            break;
        }

        // Convert token to text — use special=true for harmony to preserve special token text
        char piece[128];
        int len=llama_token_to_piece(vocab, nextToken, piece, sizeof(piece), 0, harmonyMode);
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

        int decodeResult=llama_decode(ctx, batch);
        if(decodeResult!=0)
        {
            spdlog::error("[llama] llama_decode failed during generation (token #{}, pos={}, result={})",
                i, nCur-1, decodeResult);
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

ErrorCode Llama::tokenizePrompt(llama_model *model,
    const CompletionRequest &request, const ModelInfo &modelInfo,
    std::vector<int32_t> &tokens, std::string &formattedPrompt)
{
    const llama_vocab *vocab=llama_model_get_vocab(model);
    bool harmonyMode=(modelInfo.apiFormat=="harmony");

    if(harmonyMode)
    {
        formattedPrompt=formatHarmonyPrompt(request, modelInfo);
    }
    else
    {
        formattedPrompt=applyTemplate(model, request.messages);
    }

    tokens.resize(formattedPrompt.size()+256);
    int nTokens=llama_tokenize(vocab, formattedPrompt.c_str(), formattedPrompt.length(),
        tokens.data(), tokens.size(), true, harmonyMode);
    if(nTokens<0)
    {
        tokens.resize(-nTokens);
        nTokens=llama_tokenize(vocab, formattedPrompt.c_str(), formattedPrompt.length(),
            tokens.data(), tokens.size(), true, harmonyMode);
        if(nTokens<0)
        {
            spdlog::error("Failed to tokenize prompt");
            return ErrorCode::GenerationError;
        }
    }
    tokens.resize(nTokens);
    return ErrorCode::Success;
}

ErrorCode Llama::runInferenceWithTokens(llama_model *model, llama_context *ctx,
    const CompletionRequest &request, const ModelInfo &modelInfo,
    const std::vector<int32_t> &promptTokens,
    std::string &result, int &promptTokenCount, int &completionTokens,
    double &promptTimeMs, double &generationTimeMs,
    std::function<void(const std::string &)> streamCallback)
{
    const llama_vocab *vocab=llama_model_get_vocab(model);
    bool harmonyMode=(modelInfo.apiFormat=="harmony");

    int nTokens=static_cast<int>(promptTokens.size());
    promptTokenCount=nTokens;

    spdlog::debug("[llama] clearing KV cache, prompt tokens={}", nTokens);
    llama_memory_clear(llama_get_memory(ctx), true);

    int nBatch=static_cast<int>(llama_n_batch(ctx));
    llama_batch batch=llama_batch_init(std::max(nBatch, 512), 0, 1);

    std::chrono::steady_clock::time_point promptStart=std::chrono::steady_clock::now();

    for(int start=0; start<nTokens; start+=nBatch)
    {
        int chunkSize=std::min(nBatch, nTokens-start);
        bool isLastChunk=(start+chunkSize>=nTokens);

        batch.n_tokens=chunkSize;
        for(int32_t i=0; i<chunkSize; i++)
        {
            batch.token[i]=promptTokens[start+i];
            batch.pos[i]=start+i;
            batch.n_seq_id[i]=1;
            batch.seq_id[i][0]=0;
            batch.logits[i]=0;
        }
        if(isLastChunk)
        {
            batch.logits[chunkSize-1]=1;
        }

        int decodeResult=llama_decode(ctx, batch);
        if(decodeResult!=0)
        {
            spdlog::error("[llama] llama_decode failed during prompt processing (chunk at offset {}, chunkSize={}, totalTokens={}, result={})",
                start, chunkSize, nTokens, decodeResult);
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
    }

    std::chrono::steady_clock::time_point promptEnd=std::chrono::steady_clock::now();
    promptTimeMs=std::chrono::duration<double, std::milli>(promptEnd-promptStart).count();

    int maxOutputTokens=request.max_tokens.value_or(modelInfo.maxOutputTokens);

    if(harmonyMode)
    {
        int minHarmonyTokens=std::max(maxOutputTokens*8, 16384);
        maxOutputTokens=std::min(minHarmonyTokens, modelInfo.maxOutputTokens>0?modelInfo.maxOutputTokens:131072);
    }

    int nCur=nTokens;
    completionTokens=0;

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

    for(const llama_token &token:promptTokens)
    {
        llama_sampler_accept(samplerChain, token);
    }

    std::chrono::steady_clock::time_point genStart=std::chrono::steady_clock::now();

    llama_token harmonyCallToken=-1;
    llama_token harmonyReturnToken=-1;
    if(harmonyMode)
    {
        llama_token buf[4];
        int n;

        n=llama_tokenize(vocab, "<|call|>", 8, buf, 4, false, true);
        if(n==1) harmonyCallToken=buf[0];

        n=llama_tokenize(vocab, "<|return|>", 10, buf, 4, false, true);
        if(n==1) harmonyReturnToken=buf[0];

        spdlog::debug("[llama] harmony stop tokens: <|call|>={}, <|return|>={}",
            harmonyCallToken, harmonyReturnToken);
    }

    for(int i=0; i<maxOutputTokens; ++i)
    {
        llama_token nextToken=llama_sampler_sample(samplerChain, ctx, -1);
        llama_sampler_accept(samplerChain, nextToken);

        if(harmonyMode)
        {
            if(nextToken==harmonyCallToken||nextToken==harmonyReturnToken)
            {
                if(nextToken==harmonyCallToken)
                {
                    result+="<|call|>";
                    if(streamCallback) streamCallback("<|call|>");
                }
                completionTokens++;
                break;
            }
        }

        if(llama_vocab_is_eog(vocab, nextToken))
        {
            if(harmonyMode)
            {
                spdlog::info("[llama] harmony EOG hit: token={}", nextToken);
            }
            break;
        }

        char piece[128];
        int len=llama_token_to_piece(vocab, nextToken, piece, sizeof(piece), 0, harmonyMode);
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

        if(request.stop.has_value())
        {
            bool stopFound=false;
            for(const std::string &stopWord:*request.stop)
            {
                if(result.size()>=stopWord.size()&&
                    result.substr(result.size()-stopWord.size())==stopWord)
                {
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

        batch.n_tokens=1;
        batch.token[0]=nextToken;
        batch.pos[0]=nCur;
        batch.n_seq_id[0]=1;
        batch.seq_id[0][0]=0;
        batch.logits[0]=1;
        nCur++;

        int decodeResult=llama_decode(ctx, batch);
        if(decodeResult!=0)
        {
            spdlog::error("[llama] llama_decode failed during generation (token #{}, pos={}, result={})",
                i, nCur-1, decodeResult);
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

ErrorCode Llama::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback,
    std::function<void()> waitCallback)
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

    // Tokenize prompt BEFORE acquiring the inference lock.
    // applyTemplate/formatHarmonyPrompt and llama_tokenize only need
    // llama_model/llama_vocab (read-only), not llama_context.
    spdlog::info("[llama] streaming: pre-tokenizing prompt for model '{}'", request.model);
    std::vector<int32_t> tokens;
    std::string formattedPrompt;
    ErrorCode tokenizeResult=tokenizePrompt(llamaModel, request, *modelInfo, tokens, formattedPrompt);
    if(tokenizeResult!=ErrorCode::Success)
    {
        return tokenizeResult;
    }
    spdlog::info("[llama] streaming: tokenized {} tokens, waiting for inference lock", tokens.size());

    auto lockWaitStart=std::chrono::steady_clock::now();

    runtime.beginInference(request.model);

    // Use timed try-lock loop so we can call waitCallback periodically
    // while another request holds the lock.
    bool lockAcquired=false;
    while(!lockAcquired)
    {
        lockAcquired=runtime.getInferenceMutex().try_lock_for(std::chrono::milliseconds(500));
        if(!lockAcquired&&waitCallback)
        {
            waitCallback();
        }
    }
    std::lock_guard<std::timed_mutex> inferenceLock(runtime.getInferenceMutex(), std::adopt_lock);

    auto lockWaitMs=std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now()-lockWaitStart).count();
    if(lockWaitMs>100.0)
    {
        spdlog::warn("[llama] streaming inference lock acquired after {:.1f}ms wait", lockWaitMs);
    }

    spdlog::info("[llama] starting streaming inference for model '{}' ({} prompt tokens pre-tokenized)", request.model, tokens.size());
    std::chrono::steady_clock::time_point startTime=std::chrono::steady_clock::now();

    std::string resultText;
    int promptTokens=0;
    int completionTokens=0;
    double promptTimeMs=0.0;
    double generationTimeMs=0.0;

    ErrorCode code=runInferenceWithTokens(llamaModel, llamaCtx, request, *modelInfo,
        tokens, resultText, promptTokens, completionTokens, promptTimeMs, generationTimeMs, callback);

    std::chrono::steady_clock::time_point endTime=std::chrono::steady_clock::now();
    double totalTimeMs=std::chrono::duration<double, std::milli>(endTime-startTime).count();

    runtime.endInference(request.model);

    if(code!=ErrorCode::Success)
    {
        spdlog::error("[llama] streaming inference failed for model '{}' after {:.1f}ms (error={})",
            request.model, totalTimeMs, static_cast<int>(code));
        return code;
    }

    spdlog::info("[llama] streaming inference complete: prompt={} tokens ({:.1f}ms), gen={} tokens ({:.1f}ms), total={:.1f}ms",
        promptTokens, promptTimeMs, completionTokens, generationTimeMs, totalTimeMs);

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

} // namespace arbiterAI
