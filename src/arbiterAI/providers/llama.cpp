#include "arbiterAI/providers/llama.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/telemetryCollector.h"

#include <llama.h>
#include <mtmd.h>
#include <mtmd-helper.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <algorithm>
#include <map>
#include <mutex>
#include <thread>
#include <variant>
#include <sstream>

namespace arbiterAI
{

namespace
{

/// Byte length of the largest prefix of s that ends on a complete UTF-8
/// sequence (i.e. does not split a multi-byte character). A lead byte with too
/// few following continuation bytes is held back; stray/invalid bytes are passed
/// through as-is.
size_t utf8CompleteLen(const std::string &s)
{
    size_t i=0, complete=0;
    while(i<s.size())
    {
        unsigned char c=static_cast<unsigned char>(s[i]);
        size_t need;
        if(c<0x80)            need=1; // 0xxxxxxx
        else if((c>>5)==0x6)  need=2; // 110xxxxx
        else if((c>>4)==0xE)  need=3; // 1110xxxx
        else if((c>>3)==0x1E) need=4; // 11110xxx
        else { i+=1; complete=i; continue; } // invalid lead / stray continuation
        if(i+need>s.size()) break;    // truncated multi-byte at the tail — hold back
        i+=need;
        complete=i;
    }
    return complete;
}

/// Wraps a streaming callback so it never emits a partial multi-byte UTF-8
/// character. Model tokens can split a character across token boundaries (or emit
/// a llama.cpp byte-fallback token); emitting that as a streaming delta yields
/// invalid UTF-8. Complete characters are forwarded immediately; an incomplete
/// tail is buffered until the next token completes it, and any residue is flushed
/// on destruction (end of generation).
class Utf8StreamBuffer
{
public:
    explicit Utf8StreamBuffer(const std::function<void(const std::string &)> &cb) : m_cb(cb) {}
    ~Utf8StreamBuffer() { if(m_cb && !m_pending.empty()) m_cb(m_pending); }

    void feed(const std::string &text)
    {
        if(!m_cb) return;
        m_pending+=text;
        size_t n=utf8CompleteLen(m_pending);
        if(n>0)
        {
            m_cb(m_pending.substr(0, n));
            m_pending.erase(0, n);
        }
    }

private:
    const std::function<void(const std::string &)> &m_cb;
    std::string m_pending;
};

} // namespace

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

    ErrorCode code=runInferenceDispatch(llamaModel, llamaCtx, request, model,
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

    ErrorCode code=runInferenceDispatch(llamaModel, llamaCtx, request, *modelInfo,
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

    // Embedding batches overwrite the context's KV cache — the completion
    // prefix record is no longer valid
    if(std::vector<int32_t> *kvRecord=runtime.kvCacheTokens(request.model))
    {
        kvRecord->clear();
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

std::string stripTokenStrings(const std::string &text,
    const std::vector<std::string> &tokenStrings)
{
    std::string result=text;

    for(const std::string &token:tokenStrings)
    {
        if(token.empty()) continue;

        size_t pos=0;
        while((pos=result.find(token, pos))!=std::string::npos)
        {
            result.erase(pos, token.size());
        }
    }
    return result;
}

namespace
{

/// Text of every control token in a vocab.  Walking the vocab is not free, so
/// the result is cached per vocab — vocabs live as long as their model.
const std::vector<std::string> &controlTokenStrings(const llama_vocab *vocab)
{
    static std::mutex cacheMutex;
    static std::map<const llama_vocab *, std::vector<std::string>> cache;

    std::lock_guard<std::mutex> lock(cacheMutex);

    auto it=cache.find(vocab);
    if(it!=cache.end())
    {
        return it->second;
    }

    std::vector<std::string> strings;
    int32_t count=llama_vocab_n_tokens(vocab);
    for(int32_t token=0; token<count; ++token)
    {
        if((llama_vocab_get_attr(vocab, token)&LLAMA_TOKEN_ATTR_CONTROL)==0)
        {
            continue;
        }

        const char *text=llama_vocab_get_text(vocab, token);
        if(text&&*text)
        {
            strings.push_back(text);
        }
    }

    spdlog::debug("[llama] cached {} control token strings for vocab", strings.size());
    return cache.emplace(vocab, std::move(strings)).first->second;
}

} // namespace

std::shared_ptr<ChatPrompt> Llama::applyChatFormat(const std::string &modelName,
    const CompletionRequest &request, const ModelInfo &modelInfo) const
{
    // An explicit api_format wins: it exists precisely for models whose
    // template is absent or produces the wrong thing.
    if(!modelInfo.apiFormat.empty())
    {
        return nullptr;
    }

    std::shared_ptr<ChatFormat> format=ModelRuntime::instance().getChatFormat(modelName);
    if(!format)
    {
        return nullptr;
    }

    std::vector<ToolDefinition> tools;
    if(request.tools.has_value())
    {
        tools=request.tools.value();
    }

    return format->apply(request.messages, tools);
}

std::vector<Message> Llama::sanitizeMessages(llama_model *model,
    const std::vector<Message> &messages,
    const std::vector<std::string> &extraMarkers) const
{
    const llama_vocab *vocab=llama_model_get_vocab(model);
    std::vector<std::string> markers=controlTokenStrings(vocab);
    markers.insert(markers.end(), extraMarkers.begin(), extraMarkers.end());

    std::vector<Message> sanitized;
    sanitized.reserve(messages.size());

    for(const Message &message:messages)
    {
        Message copy=message;
        copy.content=stripTokenStrings(copy.content, markers);

        for(ContentPart &part:copy.parts)
        {
            if(part.type=="text")
            {
                part.text=stripTokenStrings(part.text, markers);
            }
        }
        sanitized.push_back(std::move(copy));
    }
    return sanitized;
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

int kvPrefixReuseLength(const std::vector<int32_t> &cachedTokens,
    const std::vector<int32_t> &promptTokens)
{
    // Keep at least one prompt token to decode so the final position has
    // fresh logits for sampling
    int maxReuse=std::min(static_cast<int>(cachedTokens.size()),
        static_cast<int>(promptTokens.size())-1);
    int reused=0;
    while(reused<maxReuse&&cachedTokens[reused]==promptTokens[reused])
    {
        reused++;
    }
    return reused;
}

// Prepare the KV cache for a new prompt.  When the request sets cache_prompt
// and the previous inference's tokens share a prefix with the new prompt,
// keep that prefix in the KV cache and return how many tokens can skip
// prefill — only the divergent suffix needs decoding.  Falls back to a full
// clear otherwise.  kvRecord (owned by ModelRuntime, guarded by the
// inference mutex) is cleared here and repopulated by the caller after a
// successful inference, so any error/abort path leaves it empty and the next
// request starts from a clean cache.
static int prepareKvCache(llama_context *ctx, const CompletionRequest &request,
    const std::vector<int32_t> &promptTokens, std::vector<int32_t> *kvRecord)
{
    llama_memory_t mem=llama_get_memory(ctx);
    const int nTokens=static_cast<int>(promptTokens.size());
    int reused=0;

    if(request.cache_prompt.value_or(false)&&kvRecord&&!kvRecord->empty())
    {
        reused=kvPrefixReuseLength(*kvRecord, promptTokens);
    }

    if(reused>0&&llama_memory_seq_rm(mem, 0, reused, -1))
    {
        spdlog::info("[llama] cache_prompt: reusing {} of {} prompt tokens from KV cache",
            reused, nTokens);
    }
    else
    {
        if(reused>0)
        {
            spdlog::warn("[llama] cache_prompt: partial KV erase unsupported — full prefill");
        }
        spdlog::debug("[llama] clearing KV cache, prompt tokens={}", nTokens);
        llama_memory_clear(mem, true);
        reused=0;
    }

    if(kvRecord)
    {
        kvRecord->clear();
    }
    return reused;
}

ErrorCode Llama::runInferenceDispatch(llama_model *model, llama_context *ctx,
    const CompletionRequest &request, const ModelInfo &modelInfo,
    std::string &result, int &promptTokens, int &completionTokens,
    double &promptTimeMs, double &generationTimeMs,
    std::function<void(const std::string &)> streamCallback)
{
    if(!hasImageContent(request.messages))
    {
        return runInference(model, ctx, request, modelInfo, result,
            promptTokens, completionTokens, promptTimeMs, generationTimeMs, streamCallback);
    }

    mtmd_context *mtmdCtx=ModelRuntime::instance().getMtmdContext(request.model);
    if(!mtmdCtx)
    {
        spdlog::error("[llama] request for '{}' carries images but no projector is loaded", request.model);
        m_lastErrorDetail="model '"+request.model+"' does not accept image input";
        return ErrorCode::InvalidRequest;
    }

    MultimodalPrompt multimodal;
    std::vector<int32_t> textTokens;
    std::string formattedPrompt;

    ErrorCode tokenizeResult=tokenizeMultimodalPrompt(model, mtmdCtx, request, modelInfo,
        multimodal, textTokens, formattedPrompt);
    if(tokenizeResult!=ErrorCode::Success)
    {
        return tokenizeResult;
    }

    return runInferenceWithTokens(model, ctx, request, modelInfo, textTokens, result,
        promptTokens, completionTokens, promptTimeMs, generationTimeMs,
        streamCallback, nullptr, &multimodal);
}

ErrorCode Llama::runInference(llama_model *model, llama_context *ctx,
    const CompletionRequest &request, const ModelInfo &modelInfo,
    std::string &result, int &promptTokens, int &completionTokens,
    double &promptTimeMs, double &generationTimeMs,
    std::function<void(const std::string &)> streamCallback)
{
    const llama_vocab *vocab=llama_model_get_vocab(model);
    bool harmonyMode=(modelInfo.apiFormat=="harmony");

    // Apply chat template to format messages properly.  Content is stripped of
    // control-token markup first — see tokenizePrompt().
    CompletionRequest sanitizedRequest=request;
    sanitizedRequest.messages=sanitizeMessages(model, request.messages);

    std::string prompt;
    if(harmonyMode)
    {
        prompt=formatHarmonyPrompt(sanitizedRequest, modelInfo);
    }
    else
    {
        prompt=applyTemplate(model, sanitizedRequest.messages);
    }

    // Tokenize the formatted prompt with special-token parsing so the
    // template's control tokens become real tokens.
    std::vector<llama_token> tokensList(prompt.size()+256);
    int nTokens=llama_tokenize(vocab, prompt.c_str(), prompt.length(),
        tokensList.data(), tokensList.size(), true, true);
    if(nTokens<0)
    {
        // Buffer too small, resize and retry
        tokensList.resize(-nTokens);
        nTokens=llama_tokenize(vocab, prompt.c_str(), prompt.length(),
            tokensList.data(), tokensList.size(), true, true);
        if(nTokens<0)
        {
            spdlog::error("Failed to tokenize prompt");
            m_lastErrorDetail="failed to tokenize prompt";
            return ErrorCode::GenerationError;
        }
    }
    tokensList.resize(nTokens);
    promptTokens=nTokens;

    std::vector<int32_t> *kvRecord=ModelRuntime::instance().kvCacheTokens(request.model);
    int reusedTokens=prepareKvCache(ctx, request, tokensList, kvRecord);

    int nBatch=static_cast<int>(llama_n_batch(ctx));
    llama_batch batch=llama_batch_init(std::max(nBatch, 512), 0, 1);

    // Process prompt (timed) — chunk into n_batch-sized pieces, skipping any
    // prefix already in the KV cache
    std::chrono::steady_clock::time_point promptStart=std::chrono::steady_clock::now();

    for(int start=reusedTokens; start<nTokens; start+=nBatch)
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
            m_lastErrorDetail="llama backend failed to process the prompt (llama_decode result="
                +std::to_string(decodeResult)+", "+std::to_string(nTokens)
                +" prompt tokens) — the context may exceed the model/hardware limit or the GPU backend errored";
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
    std::vector<int32_t> generatedInKv;

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

    Utf8StreamBuffer streamBuf(streamCallback);

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
                    streamBuf.feed("<|call|>");
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

            streamBuf.feed(tokenText);
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
            m_lastErrorDetail="llama backend failed during token generation (llama_decode result="
                +std::to_string(decodeResult)+" at position "+std::to_string(nCur-1)
                +") — likely a GPU/backend error or the context was exhausted";
            llama_sampler_free(samplerChain);
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
        generatedInKv.push_back(nextToken);
    }

    std::chrono::steady_clock::time_point genEnd=std::chrono::steady_clock::now();
    generationTimeMs=std::chrono::duration<double, std::milli>(genEnd-genStart).count();

    llama_sampler_free(samplerChain);
    llama_batch_free(batch);

    // Record what now sits in the KV cache so the next cache_prompt request
    // can reuse the common prefix
    if(kvRecord)
    {
        *kvRecord=tokensList;
        kvRecord->insert(kvRecord->end(), generatedInKv.begin(), generatedInKv.end());
    }

    return ErrorCode::Success;
}

MultimodalPrompt::~MultimodalPrompt()
{
    if(m_chunks)
    {
        mtmd_input_chunks_free(m_chunks);
        m_chunks=nullptr;
    }
}

void MultimodalPrompt::reset(mtmd_context *ctx, mtmd_input_chunks *chunks)
{
    if(m_chunks&&m_chunks!=chunks)
    {
        mtmd_input_chunks_free(m_chunks);
    }
    m_ctx=ctx;
    m_chunks=chunks;
}

size_t MultimodalPrompt::tokenCount() const
{
    if(!m_chunks)
    {
        return 0;
    }
    return mtmd_helper_get_n_tokens(m_chunks);
}

ErrorCode Llama::tokenizeMultimodalPrompt(llama_model *model, mtmd_context *mtmdCtx,
    const CompletionRequest &request, const ModelInfo &modelInfo,
    MultimodalPrompt &prompt, std::vector<int32_t> &textTokens,
    std::string &formattedPrompt,
    std::shared_ptr<ChatPrompt> *chatPrompt)
{
    if(!mtmdCtx)
    {
        m_lastErrorDetail="model '"+modelInfo.model+"' has no multimodal projector loaded";
        return ErrorCode::InvalidRequest;
    }
    if(!mtmd_support_vision(mtmdCtx))
    {
        m_lastErrorDetail="the projector loaded for model '"+modelInfo.model+"' does not support image input";
        return ErrorCode::InvalidRequest;
    }

    // Rewrite each message's content so image parts become media markers, in
    // the same order the bitmaps are collected — mtmd_tokenize() matches the
    // Nth marker to the Nth bitmap.
    const char *marker=mtmd_default_marker();

    // Strip control tokens and the media marker itself from user content: an
    // injected marker would desync the marker/bitmap pairing mtmd_tokenize()
    // relies on.
    std::vector<Message> safeMessages=sanitizeMessages(model, request.messages, {marker});

    std::vector<Message> markedMessages;
    std::vector<const ContentPart *> images;
    markedMessages.reserve(safeMessages.size());

    for(const Message &msg:safeMessages)
    {
        Message marked=msg;

        if(!msg.parts.empty())
        {
            std::string content;
            for(const ContentPart &part:msg.parts)
            {
                if(part.type=="image")
                {
                    if(!content.empty()&&content.back()!='\n') content+='\n';
                    content+=marker;
                    content+='\n';
                    images.push_back(&part);
                }
                else
                {
                    content+=part.text;
                }
            }
            marked.content=content;
        }
        marked.parts.clear();
        markedMessages.push_back(std::move(marked));
    }

    if(images.empty())
    {
        m_lastErrorDetail="multimodal tokenization requested but no image parts were present";
        return ErrorCode::InvalidRequest;
    }

    formattedPrompt=applyTemplate(model, markedMessages);

    // The prompt has to come from applyTemplate here — common_chat has no
    // notion of our image parts and would drop the media markers mtmd needs.
    // Its response parser is still the right one, so derive that separately.
    if(chatPrompt)
    {
        *chatPrompt=applyChatFormat(request.model, request, modelInfo);
    }

    // Decode the image files (png/jpeg/…) into bitmaps.
    std::vector<mtmd_bitmap *> bitmaps;
    bitmaps.reserve(images.size());

    for(const ContentPart *image:images)
    {
        mtmd_bitmap *bitmap=mtmd_helper_bitmap_init_from_buf(mtmdCtx,
            image->imageData.data(), image->imageData.size());
        if(!bitmap)
        {
            for(mtmd_bitmap *b:bitmaps) mtmd_bitmap_free(b);
            spdlog::error("[llama] failed to decode image ({} bytes, mime='{}')",
                image->imageData.size(), image->mimeType);
            m_lastErrorDetail="failed to decode an input image (mime='"+image->mimeType
                +"', "+std::to_string(image->imageData.size())+" bytes) — unsupported or corrupt image data";
            return ErrorCode::InvalidRequest;
        }
        bitmaps.push_back(bitmap);
    }

    mtmd_input_chunks *chunks=mtmd_input_chunks_init();

    mtmd_input_text text;
    text.text=formattedPrompt.c_str();
    text.add_special=true;
    // The prompt is a fully templated conversation, so its control tokens must
    // tokenize as special tokens rather than literal text.
    text.parse_special=true;

    int32_t tokenizeResult=mtmd_tokenize(mtmdCtx, chunks, &text,
        const_cast<const mtmd_bitmap **>(bitmaps.data()), bitmaps.size());

    for(mtmd_bitmap *b:bitmaps)
    {
        mtmd_bitmap_free(b);
    }

    if(tokenizeResult!=0)
    {
        mtmd_input_chunks_free(chunks);
        spdlog::error("[llama] mtmd_tokenize failed (result={})", tokenizeResult);
        m_lastErrorDetail=tokenizeResult==1
            ?"number of images does not match the number of media markers in the prompt"
            :"failed to preprocess an input image for the vision encoder";
        return ErrorCode::GenerationError;
    }

    prompt.reset(mtmdCtx, chunks);

    // Collect the text-chunk tokens for the sampler; image chunks carry
    // embeddings, not token ids, so they have nothing to contribute here.
    textTokens.clear();
    size_t chunkCount=mtmd_input_chunks_size(chunks);
    for(size_t i=0; i<chunkCount; ++i)
    {
        const mtmd_input_chunk *chunk=mtmd_input_chunks_get(chunks, i);
        if(mtmd_input_chunk_get_type(chunk)!=MTMD_INPUT_CHUNK_TYPE_TEXT)
        {
            continue;
        }

        size_t nTokens=0;
        const llama_token *tokens=mtmd_input_chunk_get_tokens_text(chunk, &nTokens);
        textTokens.insert(textTokens.end(), tokens, tokens+nTokens);
    }

    spdlog::info("[llama] multimodal prompt tokenized: {} chunks, {} images, {} total tokens ({} text)",
        chunkCount, images.size(), prompt.tokenCount(), textTokens.size());

    return ErrorCode::Success;
}

ErrorCode Llama::tokenizePrompt(llama_model *model,
    const CompletionRequest &request, const ModelInfo &modelInfo,
    std::vector<int32_t> &tokens, std::string &formattedPrompt,
    std::shared_ptr<ChatPrompt> *chatPrompt)
{
    const llama_vocab *vocab=llama_model_get_vocab(model);
    bool harmonyMode=(modelInfo.apiFormat=="harmony");

    // Message content is stripped of control-token markup first, because the
    // templated prompt below is tokenized with parse_special=true.
    CompletionRequest sanitizedRequest=request;
    sanitizedRequest.messages=sanitizeMessages(model, request.messages);

    // Preferred path: let the model's own chat template render the prompt and
    // hand back the parser for its replies.  An api_format in the config is an
    // explicit override for models whose template is missing or wrong.
    std::shared_ptr<ChatPrompt> derived=applyChatFormat(request.model, sanitizedRequest, modelInfo);
    if(derived)
    {
        formattedPrompt=derived->text();
        if(chatPrompt) *chatPrompt=derived;
    }
    else if(harmonyMode)
    {
        formattedPrompt=formatHarmonyPrompt(sanitizedRequest, modelInfo);
    }
    else
    {
        formattedPrompt=applyTemplate(model, sanitizedRequest.messages);
    }

    // parse_special=true: the chat template emits the model's own control
    // tokens (<|im_start|>, <|im_end|>, …).  Tokenized as plain text they
    // neither match what the model was trained on nor terminate generation,
    // because the end-of-turn token the model then emits is text too and never
    // matches llama_vocab_is_eog().
    tokens.resize(formattedPrompt.size()+256);
    int nTokens=llama_tokenize(vocab, formattedPrompt.c_str(), formattedPrompt.length(),
        tokens.data(), tokens.size(), true, true);
    if(nTokens<0)
    {
        tokens.resize(-nTokens);
        nTokens=llama_tokenize(vocab, formattedPrompt.c_str(), formattedPrompt.length(),
            tokens.data(), tokens.size(), true, true);
        if(nTokens<0)
        {
            spdlog::error("Failed to tokenize prompt");
            m_lastErrorDetail="failed to tokenize the formatted prompt";
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
    std::function<void(const std::string &)> streamCallback,
    std::function<bool()> shouldAbort,
    const MultimodalPrompt *multimodal)
{
    const llama_vocab *vocab=llama_model_get_vocab(model);
    bool harmonyMode=(modelInfo.apiFormat=="harmony");
    const bool multimodalPrompt=(multimodal!=nullptr&&multimodal->valid());

    int nTokens=static_cast<int>(promptTokens.size());
    promptTokenCount=nTokens;

    std::vector<int32_t> *kvRecord=ModelRuntime::instance().kvCacheTokens(request.model);

    int nBatch=static_cast<int>(llama_n_batch(ctx));
    llama_batch batch=llama_batch_init(std::max(nBatch, 512), 0, 1);

    std::chrono::steady_clock::time_point promptStart=std::chrono::steady_clock::now();

    // Position of the next token to decode.  For the text path this is simply
    // the prompt length; the multimodal path gets it from mtmd (image chunks
    // advance positions differently under M-RoPE).
    int nCur=nTokens;

    if(multimodalPrompt)
    {
        // Image chunks carry embeddings rather than token ids, so there is no
        // token sequence to diff against the cache — always prefill fresh.
        llama_memory_clear(llama_get_memory(ctx), true);
        if(kvRecord)
        {
            kvRecord->clear();
        }

        promptTokenCount=static_cast<int>(multimodal->tokenCount());

        llama_pos newNPast=0;
        int32_t evalResult=mtmd_helper_eval_chunks(multimodal->context(), ctx,
            multimodal->chunks(), 0, 0, nBatch, true, &newNPast);
        if(evalResult!=0)
        {
            spdlog::error("[llama] mtmd_helper_eval_chunks failed (result={}, {} prompt tokens)",
                evalResult, promptTokenCount);
            m_lastErrorDetail="llama backend failed to process the multimodal prompt "
                "(mtmd_helper_eval_chunks result="+std::to_string(evalResult)+", "
                +std::to_string(promptTokenCount)+" prompt tokens) — the context may exceed "
                "the model/hardware limit or the vision encoder errored";
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }

        nCur=static_cast<int>(newNPast);
    }

    int reusedTokens=multimodalPrompt
        ?nTokens
        :prepareKvCache(ctx, request, promptTokens, kvRecord);

    for(int start=reusedTokens; start<nTokens; start+=nBatch)
    {
        // Check abort between prompt batches
        if(shouldAbort&&shouldAbort())
        {
            spdlog::info("[llama] inference aborted during prompt processing (batch at offset {})", start);
            llama_batch_free(batch);
            return ErrorCode::Cancelled;
        }

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
            m_lastErrorDetail="llama backend failed to process the prompt (llama_decode result="
                +std::to_string(decodeResult)+", "+std::to_string(nTokens)
                +" prompt tokens) — the context may exceed the model/hardware limit or the GPU backend errored";
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

    completionTokens=0;
    std::vector<int32_t> generatedInKv;

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

    Utf8StreamBuffer streamBuf(streamCallback);

    for(int i=0; i<maxOutputTokens; ++i)
    {
        // Check abort every token
        if(shouldAbort&&shouldAbort())
        {
            spdlog::info("[llama] inference aborted during generation (after {} tokens)", completionTokens);
            llama_sampler_free(samplerChain);
            llama_batch_free(batch);
            generationTimeMs=std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now()-genStart).count();
            return ErrorCode::Cancelled;
        }

        llama_token nextToken=llama_sampler_sample(samplerChain, ctx, -1);
        llama_sampler_accept(samplerChain, nextToken);

        if(harmonyMode)
        {
            if(nextToken==harmonyCallToken||nextToken==harmonyReturnToken)
            {
                if(nextToken==harmonyCallToken)
                {
                    result+="<|call|>";
                    streamBuf.feed("<|call|>");
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

            streamBuf.feed(tokenText);
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
            m_lastErrorDetail="llama backend failed during token generation (llama_decode result="
                +std::to_string(decodeResult)+" at position "+std::to_string(nCur-1)
                +") — likely a GPU/backend error or the context was exhausted";
            llama_sampler_free(samplerChain);
            llama_batch_free(batch);
            return ErrorCode::GenerationError;
        }
        generatedInKv.push_back(nextToken);
    }

    std::chrono::steady_clock::time_point genEnd=std::chrono::steady_clock::now();
    generationTimeMs=std::chrono::duration<double, std::milli>(genEnd-genStart).count();

    llama_sampler_free(samplerChain);
    llama_batch_free(batch);

    // Record what now sits in the KV cache so the next cache_prompt request
    // can reuse the common prefix.  A multimodal prompt has image embeddings in
    // the cache that no token sequence describes, so it records nothing and the
    // next request prefills from scratch.
    if(kvRecord&&!multimodalPrompt)
    {
        *kvRecord=promptTokens;
        kvRecord->insert(kvRecord->end(), generatedInKv.begin(), generatedInKv.end());
    }

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
