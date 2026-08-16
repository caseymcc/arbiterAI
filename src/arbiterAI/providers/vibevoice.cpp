#include "arbiterAI/providers/vibevoice.h"

#include <vibevoice_capi.h>

#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

namespace arbiterAI
{

VibeVoice::VibeVoice()
    : BaseProvider("vibevoice")
{
}

VibeVoice::~VibeVoice()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(!m_loadedModel.empty())
    {
        vv_capi_unload();
        notifyModelReleased(m_loadedModelName);
        m_loadedModel.clear();
        m_loadedModelName.clear();
    }
}

ErrorCode VibeVoice::completion(const CompletionRequest &request,
    const ModelInfo &model,
    CompletionResponse &response)
{
    return ErrorCode::NotImplemented;
}

ErrorCode VibeVoice::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    return ErrorCode::NotImplemented;
}

ErrorCode VibeVoice::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    return ErrorCode::NotImplemented;
}

std::string VibeVoice::resolveModelPath(const ModelInfo &model)
{
    return resolveDownloadableModelFile(model);
}

ErrorCode VibeVoice::synthesizeSpeech(const SpeechRequest &request,
    const ModelInfo &model,
    SpeechResponse &response)
{
    std::string ttsModel=resolveModelPath(model);
    if(ttsModel.empty())
    {
        spdlog::warn("VibeVoice provider: no model file configured for '{}'", model.model);
        return ErrorCode::ModelNotFound;
    }

    namespace fs=std::filesystem;
    fs::path modelDir=fs::path(ttsModel).parent_path();
    std::string tokenizer=(modelDir/"tokenizer.gguf").string();

    // Voice: an explicit path in the request wins; otherwise a named voice
    // (voice-<name>.gguf) if it exists; otherwise the default voice.gguf.
    std::string voicePath;
    if(request.voice.find('/')!=std::string::npos)
    {
        voicePath=request.voice;
    }
    else
    {
        fs::path named=modelDir/("voice-"+request.voice+".gguf");
        voicePath=fs::exists(named) ? named.string() : (modelDir/"voice.gguf").string();
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if(m_loadedModel!=ttsModel)
    {
        // Only one TTS model is resident at a time, so a swap releases the old.
        if(!m_loadedModel.empty())
        {
            vv_capi_unload();
            notifyModelReleased(m_loadedModelName);
            m_loadedModelName.clear();
        }
        int rc=vv_capi_load(ttsModel.c_str(), nullptr, tokenizer.c_str(), voicePath.c_str(), 0);
        if(rc!=0)
        {
            spdlog::warn("VibeVoice provider: vv_capi_load failed (rc={}) for '{}'", rc, ttsModel);
            m_loadedModel.clear();
            return ErrorCode::ModelLoadError;
        }
        m_loadedModel=ttsModel;
        m_loadedModelName=model.model;
        notifyModelLoaded(model.model);
    }

    fs::path tmpWav=fs::temp_directory_path()
        /("arbiter_vv_"+std::to_string(m_tempCounter.fetch_add(1))+".wav");

    // 0 selects vibevoice defaults: 20 diffusion steps, cfg 1.3, 200 max frames,
    // random seed.
    int rc=vv_capi_tts(request.input.c_str(), voicePath.c_str(), nullptr, 0,
        tmpWav.string().c_str(), 0, 0.0f, 0, 0);
    if(rc!=0)
    {
        spdlog::warn("VibeVoice provider: vv_capi_tts failed (rc={})", rc);
        std::error_code ec;
        fs::remove(tmpWav, ec);
        return ErrorCode::GenerationError;
    }

    std::ifstream in(tmpWav, std::ios::binary);
    if(!in)
    {
        spdlog::warn("VibeVoice provider: could not read output WAV '{}'", tmpWav.string());
        return ErrorCode::GenerationError;
    }
    response.audio.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    in.close();

    std::error_code ec;
    fs::remove(tmpWav, ec);

    if(response.audio.empty())
        return ErrorCode::GenerationError;

    response.format="wav";
    response.model=model.model;
    response.provider="vibevoice";

    return ErrorCode::Success;
}

} // namespace arbiterAI
