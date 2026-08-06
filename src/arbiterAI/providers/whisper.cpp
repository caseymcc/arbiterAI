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
        error="Unsupported WAV: only uncompressed 16-bit PCM is supported "
            "(transcode compressed formats to PCM WAV before calling)";
        return ErrorCode::InvalidRequest;
    }
    if(numChannels<1 || sampleRate==0)
    {
        error="Malformed WAV: invalid channel count or sample rate";
        return ErrorCode::InvalidRequest;
    }

    // Decode interleaved int16 -> float, downmixing any channel count to mono.
    const size_t totalSamples=dataSize/2;
    const size_t frames=totalSamples/numChannels;
    std::vector<float> mono(frames);
    for(size_t f=0; f<frames; ++f)
    {
        float acc=0.0f;
        for(uint16_t c=0; c<numChannels; ++c)
        {
            int16_t s=static_cast<int16_t>(readLE(bytes, dataOffset+(f*numChannels+c)*2, 2));
            acc+=static_cast<float>(s)/32768.0f;
        }
        mono[f]=acc/numChannels;
    }

    // Resample to whisper's required 16 kHz (linear interpolation).
    if(sampleRate==static_cast<uint32_t>(WHISPER_SAMPLE_RATE_HZ) || frames==0)
    {
        samplesOut=std::move(mono);
    }
    else
    {
        const double ratio=static_cast<double>(WHISPER_SAMPLE_RATE_HZ)/sampleRate;
        const size_t outN=static_cast<size_t>(frames*ratio);
        samplesOut.resize(outN);
        for(size_t i=0; i<outN; ++i)
        {
            double srcPos=i/ratio;
            size_t idx=static_cast<size_t>(srcPos);
            double frac=srcPos-idx;
            float a=mono[std::min(idx, frames-1)];
            float b=mono[std::min(idx+1, frames-1)];
            samplesOut[i]=a+(b-a)*static_cast<float>(frac);
        }
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

    const bool wantWords=request.wordTimestamps.value_or(false);
    const bool diarize=request.diarize.value_or(false);
    wparams.token_timestamps=wantWords;     // per-token times for word timestamps
    wparams.tdrz_enable=diarize;            // tinydiarize speaker-turn detection (needs a *.tdrz model)

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
        int nSeg=whisper_full_n_segments(ctx);
        int speaker=0;
        for(int i=0; i<nSeg; ++i)
        {
            const char *segText=whisper_full_get_segment_text(ctx, i);
            std::string segStr=segText ? segText : "";
            text+=segStr;

            TranscriptionSegment seg;
            seg.id=i;
            seg.start=whisper_full_get_segment_t0(ctx, i)/100.0; // centiseconds -> seconds
            seg.end=whisper_full_get_segment_t1(ctx, i)/100.0;
            seg.text=segStr;
            if(diarize)
                seg.speaker=speaker;

            if(wantWords)
            {
                // Merge sub-word tokens into words (a new word begins at a token
                // whose text starts with a space). Skip special/timestamp tokens.
                std::string cur;
                double curStart=seg.start, curEnd=seg.start;
                bool have=false;
                auto flush=[&]()
                {
                    if(!have || cur.empty()) { have=false; cur.clear(); return; }
                    TranscriptionWord w;
                    w.word=cur; w.start=curStart; w.end=curEnd;
                    if(diarize) w.speaker=speaker;
                    seg.words.push_back(std::move(w));
                    have=false; cur.clear();
                };
                int nTok=whisper_full_n_tokens(ctx, i);
                for(int j=0; j<nTok; ++j)
                {
                    const char *tt=whisper_full_get_token_text(ctx, i, j);
                    if(!tt) continue;
                    std::string t=tt;
                    if(t.empty() || t[0]=='[') continue; // special / timestamp token
                    whisper_token_data td=whisper_full_get_token_data(ctx, i, j);
                    if(!t.empty() && t[0]==' ')
                    {
                        flush();
                        curStart=td.t0/100.0;
                        have=true;
                        cur=t.substr(1);
                    }
                    else
                    {
                        if(!have) { curStart=td.t0/100.0; have=true; }
                        cur+=t;
                    }
                    curEnd=td.t1/100.0;
                }
                flush();
            }

            response.segments.push_back(std::move(seg));

            if(diarize && whisper_full_get_segment_speaker_turn_next(ctx, i))
                ++speaker;
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
