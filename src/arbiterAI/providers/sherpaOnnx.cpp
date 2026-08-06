#include "arbiterAI/providers/sherpaOnnx.h"
#include "arbiterAI/storageManager.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace arbiterAI
{

namespace
{

constexpr int SHERPA_SAMPLE_RATE_HZ=16000;

uint32_t readLE(const std::vector<uint8_t> &b, size_t offset, size_t width)
{
    uint32_t value=0;
    for(size_t i=0; i<width; ++i)
        value|=static_cast<uint32_t>(b[offset+i])<<(8*i);
    return value;
}

/// Decode a 16-bit PCM WAV to 16 kHz mono float (downmix + linear resample).
bool decodeWav16k(const std::vector<uint8_t> &bytes, std::vector<float> &out, std::string &error)
{
    if(bytes.size()<44 || std::memcmp(bytes.data(), "RIFF", 4)!=0
        || std::memcmp(bytes.data()+8, "WAVE", 4)!=0)
    {
        error="Unsupported audio: expected a WAV (RIFF/WAVE) file";
        return false;
    }
    uint16_t audioFormat=0, numChannels=0, bitsPerSample=0;
    uint32_t sampleRate=0;
    size_t dataOffset=0, dataSize=0;
    bool haveFmt=false, haveData=false;
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
        pos=body+chunkSize+(chunkSize&1);
    }
    if(!haveFmt || !haveData || audioFormat!=1 || bitsPerSample!=16 || numChannels<1 || sampleRate==0)
    {
        error="Unsupported WAV: expected uncompressed 16-bit PCM";
        return false;
    }
    const size_t frames=(dataSize/2)/numChannels;
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
    if(sampleRate==static_cast<uint32_t>(SHERPA_SAMPLE_RATE_HZ) || frames==0)
    {
        out=std::move(mono);
    }
    else
    {
        const double ratio=static_cast<double>(SHERPA_SAMPLE_RATE_HZ)/sampleRate;
        const size_t outN=static_cast<size_t>(frames*ratio);
        out.resize(outN);
        for(size_t i=0; i<outN; ++i)
        {
            double srcPos=i/ratio;
            size_t idx=static_cast<size_t>(srcPos);
            double frac=srcPos-idx;
            float a=mono[std::min(idx, frames-1)];
            float b=mono[std::min(idx+1, frames-1)];
            out[i]=a+(b-a)*static_cast<float>(frac);
        }
    }
    return true;
}

/// Resolve p against base dir when not absolute.
std::string resolveRel(const std::string &base, const std::string &p)
{
    if(p.empty()) return p;
    std::filesystem::path pp(p);
    if(pp.is_absolute() || base.empty()) return p;
    return (std::filesystem::path(base)/pp).string();
}

std::string jstr(const nlohmann::json &j, const char *key, const std::string &def="")
{
    return (j.is_object() && j.contains(key) && j[key].is_string()) ? j[key].get<std::string>() : def;
}

} // namespace

SherpaOnnx::SherpaOnnx()
    : BaseProvider("sherpa-onnx")
{
}

SherpaOnnx::~SherpaOnnx()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for(auto &e:m_recognizers)
        if(e.second) SherpaOnnxDestroyOfflineRecognizer(e.second);
    m_recognizers.clear();
    for(auto &e:m_embedders)
        if(e.second) SherpaOnnxDestroySpeakerEmbeddingExtractor(e.second);
    m_embedders.clear();
    for(auto &e:m_taggers)
        if(e.second) SherpaOnnxDestroyAudioTagging(e.second);
    m_taggers.clear();
}

ErrorCode SherpaOnnx::completion(const CompletionRequest &, const ModelInfo &, CompletionResponse &)
{ return ErrorCode::NotImplemented; }

ErrorCode SherpaOnnx::streamingCompletion(const CompletionRequest &, std::function<void(const std::string &)>)
{ return ErrorCode::NotImplemented; }

ErrorCode SherpaOnnx::getEmbeddings(const EmbeddingRequest &, EmbeddingResponse &)
{ return ErrorCode::NotImplemented; }

const SherpaOnnxOfflineRecognizer *SherpaOnnx::acquireRecognizer(const ModelInfo &model)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it=m_recognizers.find(model.model);
    if(it!=m_recognizers.end())
        return it->second;

    const nlohmann::json &opts=model.sherpaOptions;
    if(!opts.is_object())
    {
        spdlog::warn("SherpaOnnx: model '{}' has no sherpa_options", model.model);
        return nullptr;
    }

    std::string base;
    if(model.filePath.has_value() && !model.filePath->empty())
        base=std::filesystem::path(model.filePath.value()).parent_path().string();
    else
        base=StorageManager::instance().getModelsDir().string();

    // Strings must outlive the SherpaOnnxCreateOfflineRecognizer() call.
    std::string tokens=resolveRel(base, jstr(opts, "tokens"));
    std::string decoding=jstr(opts, "decoding_method", "greedy_search");
    std::string provider="cpu";
    std::string modelType=jstr(opts, "model_type", "whisper");
    std::string enc, dec, joiner, mdl, lang, task, pre, unc, cac;

    SherpaOnnxOfflineRecognizerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate=SHERPA_SAMPLE_RATE_HZ;
    config.feat_config.feature_dim=opts.value("feature_dim", 80);
    config.model_config.tokens=tokens.c_str();
    config.model_config.num_threads=opts.value("num_threads", 2);
    config.model_config.provider=provider.c_str();
    config.model_config.debug=0;
    config.decoding_method=decoding.c_str();

    if(modelType=="whisper")
    {
        const nlohmann::json &w=opts.value("whisper", nlohmann::json::object());
        enc=resolveRel(base, jstr(w, "encoder"));
        dec=resolveRel(base, jstr(w, "decoder"));
        lang=jstr(w, "language");
        task=jstr(w, "task", "transcribe");
        config.model_config.whisper.encoder=enc.c_str();
        config.model_config.whisper.decoder=dec.c_str();
        config.model_config.whisper.language=lang.c_str();
        config.model_config.whisper.task=task.c_str();
        config.model_config.whisper.tail_paddings=w.value("tail_paddings", -1);
    }
    else if(modelType=="sense_voice")
    {
        const nlohmann::json &sv=opts.value("sense_voice", nlohmann::json::object());
        mdl=resolveRel(base, jstr(sv, "model"));
        lang=jstr(sv, "language", "auto");
        config.model_config.sense_voice.model=mdl.c_str();
        config.model_config.sense_voice.language=lang.c_str();
        config.model_config.sense_voice.use_itn=sv.value("use_itn", 1);
    }
    else if(modelType=="paraformer")
    {
        mdl=resolveRel(base, jstr(opts.value("paraformer", nlohmann::json::object()), "model"));
        config.model_config.paraformer.model=mdl.c_str();
    }
    else if(modelType=="nemo_ctc")
    {
        mdl=resolveRel(base, jstr(opts.value("nemo_ctc", nlohmann::json::object()), "model"));
        config.model_config.nemo_ctc.model=mdl.c_str();
    }
    else if(modelType=="moonshine")
    {
        const nlohmann::json &mo=opts.value("moonshine", nlohmann::json::object());
        pre=resolveRel(base, jstr(mo, "preprocessor"));
        enc=resolveRel(base, jstr(mo, "encoder"));
        unc=resolveRel(base, jstr(mo, "uncached_decoder"));
        cac=resolveRel(base, jstr(mo, "cached_decoder"));
        config.model_config.moonshine.preprocessor=pre.c_str();
        config.model_config.moonshine.encoder=enc.c_str();
        config.model_config.moonshine.uncached_decoder=unc.c_str();
        config.model_config.moonshine.cached_decoder=cac.c_str();
    }
    else if(modelType=="transducer")
    {
        const nlohmann::json &t=opts.value("transducer", nlohmann::json::object());
        enc=resolveRel(base, jstr(t, "encoder"));
        dec=resolveRel(base, jstr(t, "decoder"));
        joiner=resolveRel(base, jstr(t, "joiner"));
        config.model_config.transducer.encoder=enc.c_str();
        config.model_config.transducer.decoder=dec.c_str();
        config.model_config.transducer.joiner=joiner.c_str();
    }
    else
    {
        spdlog::warn("SherpaOnnx: unknown model_type '{}' for '{}'", modelType, model.model);
        return nullptr;
    }

    const SherpaOnnxOfflineRecognizer *rec=SherpaOnnxCreateOfflineRecognizer(&config);
    if(!rec)
    {
        spdlog::warn("SherpaOnnx: failed to create recognizer for '{}'", model.model);
        return nullptr;
    }
    m_recognizers.emplace(model.model, rec);
    return rec;
}

ErrorCode SherpaOnnx::transcribe(const AudioTranscriptionRequest &request,
    const ModelInfo &model,
    AudioTranscriptionResponse &response)
{
    std::vector<float> samples;
    std::string decodeError;
    if(!decodeWav16k(request.audio, samples, decodeError))
    {
        spdlog::warn("SherpaOnnx: {}", decodeError);
        return ErrorCode::InvalidRequest;
    }

    const SherpaOnnxOfflineRecognizer *rec=acquireRecognizer(model);
    if(!rec)
        return ErrorCode::ModelLoadError;

    const bool wantWords=request.wordTimestamps.value_or(false);
    const double duration=samples.empty() ? 0.0
        : static_cast<double>(samples.size())/SHERPA_SAMPLE_RATE_HZ;

    std::lock_guard<std::mutex> lock(m_inferenceMutex);
    const SherpaOnnxOfflineStream *stream=SherpaOnnxCreateOfflineStream(rec);
    if(!stream)
        return ErrorCode::GenerationError;
    SherpaOnnxAcceptWaveformOffline(stream, SHERPA_SAMPLE_RATE_HZ,
        samples.data(), static_cast<int32_t>(samples.size()));
    SherpaOnnxDecodeOfflineStream(rec, stream);

    const SherpaOnnxOfflineRecognizerResult *r=SherpaOnnxGetOfflineStreamResult(stream);
    if(r)
    {
        response.text=r->text ? r->text : "";

        TranscriptionSegment seg;
        seg.id=0;
        seg.start=0.0;
        seg.end=duration;
        seg.text=response.text;

        if(wantWords && r->timestamps && r->tokens_arr && r->count>0)
        {
            // Merge sub-word tokens into words (a new word begins at a token that
            // starts with a space or the BPE '▁' marker).
            std::string cur;
            double curStart=0.0;
            bool have=false;
            auto flush=[&](double end)
            {
                if(!have || cur.empty()) { have=false; cur.clear(); return; }
                TranscriptionWord w;
                w.word=cur; w.start=curStart; w.end=end;
                seg.words.push_back(std::move(w));
                have=false; cur.clear();
            };
            for(int32_t i=0; i<r->count; ++i)
            {
                const char *tk=r->tokens_arr[i];
                if(!tk) continue;
                std::string t=tk;
                double ts=r->timestamps[i];
                bool newWord=(!t.empty() && (t[0]==' ' || t.rfind("\xE2\x96\x81", 0)==0));
                if(newWord)
                {
                    flush(ts);
                    curStart=ts;
                    have=true;
                    // strip leading space or the 3-byte U+2581 marker
                    cur=(t[0]==' ') ? t.substr(1) : t.substr(3);
                }
                else
                {
                    if(!have) { curStart=ts; have=true; }
                    cur+=t;
                }
            }
            flush(duration);
        }

        response.segments.push_back(std::move(seg));
        SherpaOnnxDestroyOfflineRecognizerResult(r);
    }

    SherpaOnnxDestroyOfflineStream(stream);

    response.model=model.model;
    response.provider="sherpa-onnx";
    response.duration=duration;
    return ErrorCode::Success;
}

const SherpaOnnxSpeakerEmbeddingExtractor *SherpaOnnx::acquireEmbedder(const ModelInfo &model)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it=m_embedders.find(model.model);
    if(it!=m_embedders.end())
        return it->second;

    const nlohmann::json &opts=model.sherpaOptions;
    if(!opts.is_object())
    {
        spdlog::warn("SherpaOnnx: model '{}' has no sherpa_options", model.model);
        return nullptr;
    }

    std::string base;
    if(model.filePath.has_value() && !model.filePath->empty())
        base=std::filesystem::path(model.filePath.value()).parent_path().string();
    else
        base=StorageManager::instance().getModelsDir().string();

    // Strings must outlive the SherpaOnnxCreateSpeakerEmbeddingExtractor() call.
    const nlohmann::json &se=opts.value("speaker_embedding", nlohmann::json::object());
    std::string mdl=resolveRel(base, jstr(se, "model"));
    if(mdl.empty())
        mdl=resolveRel(base, jstr(opts, "model")); // allow a top-level "model"
    std::string provider="cpu";

    SherpaOnnxSpeakerEmbeddingExtractorConfig config;
    std::memset(&config, 0, sizeof(config));
    config.model=mdl.c_str();
    config.num_threads=opts.value("num_threads", 2);
    config.provider=provider.c_str();
    config.debug=0;

    const SherpaOnnxSpeakerEmbeddingExtractor *ex=SherpaOnnxCreateSpeakerEmbeddingExtractor(&config);
    if(!ex)
    {
        spdlog::warn("SherpaOnnx: failed to create speaker-embedding extractor for '{}'", model.model);
        return nullptr;
    }
    m_embedders.emplace(model.model, ex);
    return ex;
}

ErrorCode SherpaOnnx::embedAudio(const AudioEmbeddingRequest &request,
    const ModelInfo &model,
    AudioEmbeddingResponse &response)
{
    std::vector<float> samples;
    std::string decodeError;
    if(!decodeWav16k(request.audio, samples, decodeError))
    {
        spdlog::warn("SherpaOnnx: {}", decodeError);
        return ErrorCode::InvalidRequest;
    }

    const SherpaOnnxSpeakerEmbeddingExtractor *ex=acquireEmbedder(model);
    if(!ex)
        return ErrorCode::ModelLoadError;

    const double duration=samples.empty() ? 0.0
        : static_cast<double>(samples.size())/SHERPA_SAMPLE_RATE_HZ;

    std::lock_guard<std::mutex> lock(m_inferenceMutex);
    const SherpaOnnxOnlineStream *stream=SherpaOnnxSpeakerEmbeddingExtractorCreateStream(ex);
    if(!stream)
        return ErrorCode::GenerationError;
    SherpaOnnxOnlineStreamAcceptWaveform(stream, SHERPA_SAMPLE_RATE_HZ,
        samples.data(), static_cast<int32_t>(samples.size()));
    SherpaOnnxOnlineStreamInputFinished(stream);

    if(!SherpaOnnxSpeakerEmbeddingExtractorIsReady(ex, stream))
    {
        SherpaOnnxDestroyOnlineStream(stream);
        spdlog::warn("SherpaOnnx: too little audio for a speaker embedding ({:.2f}s)", duration);
        return ErrorCode::InvalidRequest;
    }

    const float *emb=SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding(ex, stream);
    const int32_t dim=SherpaOnnxSpeakerEmbeddingExtractorDim(ex);
    if(emb && dim>0)
        response.embedding.assign(emb, emb+dim);
    if(emb)
        SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding(emb);
    SherpaOnnxDestroyOnlineStream(stream);

    if(response.embedding.empty())
        return ErrorCode::GenerationError;

    response.model=model.model;
    response.provider="sherpa-onnx";
    response.duration=duration;
    return ErrorCode::Success;
}

const SherpaOnnxAudioTagging *SherpaOnnx::acquireTagger(const ModelInfo &model)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it=m_taggers.find(model.model);
    if(it!=m_taggers.end())
        return it->second;

    const nlohmann::json &opts=model.sherpaOptions;
    if(!opts.is_object())
    {
        spdlog::warn("SherpaOnnx: model '{}' has no sherpa_options", model.model);
        return nullptr;
    }

    std::string base;
    if(model.filePath.has_value() && !model.filePath->empty())
        base=std::filesystem::path(model.filePath.value()).parent_path().string();
    else
        base=StorageManager::instance().getModelsDir().string();

    // Strings must outlive the SherpaOnnxCreateAudioTagging() call.
    const nlohmann::json &at=opts.value("audio_tagging", nlohmann::json::object());
    std::string ced=resolveRel(base, jstr(at, "ced"));
    std::string zipformer=resolveRel(base, jstr(at, "zipformer"));
    std::string labels=resolveRel(base, jstr(at, "labels"));
    std::string provider="cpu";

    SherpaOnnxAudioTaggingConfig config;
    std::memset(&config, 0, sizeof(config));
    config.model.num_threads=at.value("num_threads", opts.value("num_threads", 2));
    config.model.provider=provider.c_str();
    config.model.debug=0;
    if(!ced.empty())
        config.model.ced=ced.c_str();
    if(!zipformer.empty())
        config.model.zipformer.model=zipformer.c_str();
    config.labels=labels.c_str();
    config.top_k=at.value("top_k", 5);

    const SherpaOnnxAudioTagging *tg=SherpaOnnxCreateAudioTagging(&config);
    if(!tg)
    {
        spdlog::warn("SherpaOnnx: failed to create audio tagger for '{}'", model.model);
        return nullptr;
    }
    m_taggers.emplace(model.model, tg);
    return tg;
}

ErrorCode SherpaOnnx::classifyAudio(const AudioClassificationRequest &request,
    const ModelInfo &model,
    AudioClassificationResponse &response)
{
    std::vector<float> samples;
    std::string decodeError;
    if(!decodeWav16k(request.audio, samples, decodeError))
    {
        spdlog::warn("SherpaOnnx: {}", decodeError);
        return ErrorCode::InvalidRequest;
    }

    const SherpaOnnxAudioTagging *tagger=acquireTagger(model);
    if(!tagger)
        return ErrorCode::ModelLoadError;

    const double duration=samples.empty() ? 0.0
        : static_cast<double>(samples.size())/SHERPA_SAMPLE_RATE_HZ;
    const int32_t topK=request.topK.value_or(-1); // -1 → use the config's top_k

    std::lock_guard<std::mutex> lock(m_inferenceMutex);
    const SherpaOnnxOfflineStream *stream=SherpaOnnxAudioTaggingCreateOfflineStream(tagger);
    if(!stream)
        return ErrorCode::GenerationError;
    SherpaOnnxAcceptWaveformOffline(stream, SHERPA_SAMPLE_RATE_HZ,
        samples.data(), static_cast<int32_t>(samples.size()));

    const SherpaOnnxAudioEvent *const *results=SherpaOnnxAudioTaggingCompute(tagger, stream, topK);
    if(results)
    {
        for(const SherpaOnnxAudioEvent *const *p=results; *p!=nullptr; ++p)
        {
            AudioTag tag;
            tag.label=(*p)->name ? (*p)->name : "";
            tag.score=(*p)->prob;
            response.tags.push_back(std::move(tag));
        }
        SherpaOnnxAudioTaggingFreeResults(results);
    }
    SherpaOnnxDestroyOfflineStream(stream);

    response.model=model.model;
    response.provider="sherpa-onnx";
    response.duration=duration;
    return ErrorCode::Success;
}

} // namespace arbiterAI
