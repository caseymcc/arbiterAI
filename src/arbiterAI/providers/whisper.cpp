#include "arbiterAI/providers/whisper.h"

#include <whisper.h>

#include <spdlog/spdlog.h>
#include <cstring>

namespace arbiterAI
{

namespace
{

/// Read a little-endian unsigned integer of the given width from a byte buffer.
uint32_t readLE(const std::vector<uint8_t> &b, size_t offset, size_t width)
{
    uint32_t value=0;
    for(size_t i=0; i<width; ++i)
        value|=static_cast<uint32_t>(b[offset+i])<<(8*i);
    return value;
}

constexpr int WHISPER_SAMPLE_RATE_HZ=16000;

} // namespace

Whisper::Whisper()
    : BaseProvider("whisper")
{
}

Whisper::~Whisper()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for(auto &entry:m_contexts)
    {
        if(entry.second)
            whisper_free(entry.second);
    }
    m_contexts.clear();
}

ErrorCode Whisper::completion(const CompletionRequest &request,
    const ModelInfo &model,
    CompletionResponse &response)
{
    // whisper is a speech-to-text engine; text completion is not supported.
    return ErrorCode::NotImplemented;
}

ErrorCode Whisper::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    return ErrorCode::NotImplemented;
}

ErrorCode Whisper::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    return ErrorCode::NotImplemented;
}

std::string Whisper::resolveModelPath(const ModelInfo &model)
{
    return resolveDownloadableModelFile(model);
}

whisper_context *Whisper::acquireContext(const std::string &modelPath)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it=m_contexts.find(modelPath);
    if(it!=m_contexts.end())
        return it->second;

    whisper_context_params cparams=whisper_context_default_params();
    whisper_context *ctx=whisper_init_from_file_with_params(modelPath.c_str(), cparams);
    if(!ctx)
    {
        spdlog::warn("Whisper provider: failed to load model from '{}'", modelPath);
        return nullptr;
    }

    m_contexts.emplace(modelPath, ctx);
    return ctx;
}

ErrorCode Whisper::decodeAudio(const std::vector<uint8_t> &bytes,
    std::vector<float> &samplesOut, std::string &error)
{
    // Minimal RIFF/WAVE parser: supports uncompressed 16-bit PCM, 16 kHz, mono.
    if(bytes.size()<44
        || std::memcmp(bytes.data(), "RIFF", 4)!=0
        || std::memcmp(bytes.data()+8, "WAVE", 4)!=0)
    {
        error="Unsupported audio: expected a WAV (RIFF/WAVE) file";
        return ErrorCode::InvalidRequest;
    }

    uint16_t audioFormat=0, numChannels=0, bitsPerSample=0;
    uint32_t sampleRate=0;
    size_t dataOffset=0, dataSize=0;
    bool haveFmt=false, haveData=false;

    // Walk the chunk list starting after the 12-byte RIFF header.
    size_t pos=12;
    while(pos+8<=bytes.size())
    {
        const char *id=reinterpret_cast<const char *>(bytes.data()+pos);
        uint32_t chunkSize=readLE(bytes, pos+4, 4);
        size_t body=pos+8;

        if(std::memcmp(id, "fmt ", 4)==0 && body+16<=bytes.size())
        {
            audioFormat=static_cast<uint16_t>(readLE(bytes, body, 2));
            numChannels=static_cast<uint16_t>(readLE(bytes, body+2, 2));
            sampleRate=readLE(bytes, body+4, 4);
            bitsPerSample=static_cast<uint16_t>(readLE(bytes, body+14, 2));
            haveFmt=true;
        }
        else if(std::memcmp(id, "data", 4)==0)
        {
            dataOffset=body;
            dataSize=std::min<size_t>(chunkSize, bytes.size()-body);
            haveData=true;
        }

        // Chunks are word-aligned (padded to even size).
        pos=body+chunkSize+(chunkSize&1);
    }

    if(!haveFmt || !haveData)
    {
        error="Malformed WAV: missing 'fmt ' or 'data' chunk";
        return ErrorCode::InvalidRequest;
    }
    if(audioFormat!=1 || bitsPerSample!=16)
    {
        error="Unsupported WAV: only uncompressed 16-bit PCM is supported";
        return ErrorCode::InvalidRequest;
    }
    if(numChannels!=1 || sampleRate!=static_cast<uint32_t>(WHISPER_SAMPLE_RATE_HZ))
    {
        error="Unsupported WAV: expected 16 kHz mono (transcode before calling)";
        return ErrorCode::InvalidRequest;
    }

    const size_t sampleCount=dataSize/2;
    samplesOut.resize(sampleCount);
    for(size_t i=0; i<sampleCount; ++i)
    {
        int16_t s=static_cast<int16_t>(readLE(bytes, dataOffset+i*2, 2));
        samplesOut[i]=static_cast<float>(s)/32768.0f;
    }

    return ErrorCode::Success;
}

ErrorCode Whisper::transcribe(const AudioTranscriptionRequest &request,
    const ModelInfo &model,
    AudioTranscriptionResponse &response)
{
    std::string modelPath=resolveModelPath(model);
    if(modelPath.empty())
    {
        spdlog::warn("Whisper provider: no model file configured for '{}'", model.model);
        return ErrorCode::ModelNotFound;
    }

    std::vector<float> samples;
    std::string decodeError;
    ErrorCode decodeResult=decodeAudio(request.audio, samples, decodeError);
    if(decodeResult!=ErrorCode::Success)
    {
        spdlog::warn("Whisper provider: {}", decodeError);
        return decodeResult;
    }

    whisper_context *ctx=acquireContext(modelPath);
    if(!ctx)
        return ErrorCode::ModelLoadError;

    whisper_full_params wparams=whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress=false;
    wparams.print_realtime=false;
    wparams.print_special=false;
    wparams.print_timestamps=false;

    // Language selection: request hint, else the model's whisper_options default,
    // else auto-detect. `translate` comes from whisper_options.
    std::string defaultLanguage="auto";
    bool translate=false;
    if(model.whisperOptions.is_object())
    {
        defaultLanguage=model.whisperOptions.value("language", std::string("auto"));
        translate=model.whisperOptions.value("translate", false);
    }
    std::string language=request.language.value_or(defaultLanguage);
    wparams.language=language.c_str();
    wparams.detect_language=(language=="auto");
    wparams.translate=translate;

    // whisper_full is not reentrant on a shared context — serialize inference.
    {
        std::lock_guard<std::mutex> lock(m_inferenceMutex);
        int rc=whisper_full(ctx, wparams, samples.data(), static_cast<int>(samples.size()));
        if(rc!=0)
        {
            spdlog::warn("Whisper provider: whisper_full failed (rc={})", rc);
            return ErrorCode::GenerationError;
        }

        std::string text;
        int segments=whisper_full_n_segments(ctx);
        for(int i=0; i<segments; ++i)
        {
            const char *segText=whisper_full_get_segment_text(ctx, i);
            if(segText)
                text+=segText;
        }
        response.text=std::move(text);

        int langId=whisper_full_lang_id(ctx);
        if(langId>=0)
        {
            const char *langStr=whisper_lang_str(langId);
            if(langStr)
                response.language=langStr;
        }
    }

    response.model=model.model;
    response.provider="whisper";
    response.duration=static_cast<double>(samples.size())/WHISPER_SAMPLE_RATE_HZ;

    return ErrorCode::Success;
}

} // namespace arbiterAI
