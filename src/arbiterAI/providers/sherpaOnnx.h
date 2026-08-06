#ifndef _arbiterAI_providers_sherpaOnnx_h_
#define _arbiterAI_providers_sherpaOnnx_h_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"

#include <map>
#include <mutex>
#include <string>

struct SherpaOnnxOfflineRecognizer;
struct SherpaOnnxSpeakerEmbeddingExtractor;
struct SherpaOnnxAudioTagging;

namespace arbiterAI
{

/**
 * @class SherpaOnnx
 * @brief Local speech-to-text provider backed by sherpa-onnx (onnxruntime)
 *
 * Handles models with `"provider": "sherpa-onnx"` and `"mode": "transcription"`.
 * A single provider serves many model families — Whisper, SenseVoice, Paraformer,
 * NeMo-CTC, Moonshine, and Zipformer/transducer — selected by a `sherpa_options`
 * block in the model config:
 *
 *   "sherpa_options": {
 *     "model_type": "whisper",           // whisper|sense_voice|paraformer|nemo_ctc|moonshine|transducer
 *     "tokens": "tokens.txt",
 *     "num_threads": 2,
 *     "decoding_method": "greedy_search",
 *     "whisper": { "encoder": "...", "decoder": "...", "language": "en", "task": "transcribe" }
 *   }
 *
 * Paths are resolved relative to the model config's `file_path` directory (or the
 * StorageManager models dir) when not absolute. Emits word-level timestamps into
 * AudioTranscriptionResponse::segments. Recognizers are cached per model.
 */
class SherpaOnnx : public BaseProvider
{
public:
    SherpaOnnx();
    ~SherpaOnnx() override;

    ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;

    ErrorCode transcribe(const AudioTranscriptionRequest &request,
        const ModelInfo &model,
        AudioTranscriptionResponse &response) override;

    /// Speaker-embedding (voice fingerprint). Model config uses
    /// `"mode": "audio_embedding"` and `sherpa_options.model_type == "speaker_embedding"`
    /// with `sherpa_options.speaker_embedding.model` pointing at a wespeaker/
    /// 3d-speaker ONNX. Emits a fixed-length L2-comparable vector.
    ErrorCode embedAudio(const AudioEmbeddingRequest &request,
        const ModelInfo &model,
        AudioEmbeddingResponse &response) override;

    /// Audio tagging (sound events: music, speech, TV, …). Model config uses
    /// `"mode": "audio_classification"` and `sherpa_options.model_type == "audio_tagging"`
    /// with `sherpa_options.audio_tagging.{ced|zipformer, labels, top_k}`.
    ErrorCode classifyAudio(const AudioClassificationRequest &request,
        const ModelInfo &model,
        AudioClassificationResponse &response) override;

private:
    /// Get (building on first use) a recognizer for the model. Returns nullptr on
    /// failure. Thread-safe.
    const SherpaOnnxOfflineRecognizer *acquireRecognizer(const ModelInfo &model);

    /// Get (building on first use) a speaker-embedding extractor. Thread-safe.
    const SherpaOnnxSpeakerEmbeddingExtractor *acquireEmbedder(const ModelInfo &model);

    /// Get (building on first use) an audio-tagging tagger. Thread-safe.
    const SherpaOnnxAudioTagging *acquireTagger(const ModelInfo &model);

    std::mutex m_mutex;              ///< Guards the recognizer / embedder / tagger caches
    std::mutex m_inferenceMutex;     ///< Serializes decode / embedding / tagging (v1)
    std::map<std::string, const SherpaOnnxOfflineRecognizer *> m_recognizers;
    std::map<std::string, const SherpaOnnxSpeakerEmbeddingExtractor *> m_embedders;
    std::map<std::string, const SherpaOnnxAudioTagging *> m_taggers;
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_sherpaOnnx_h_
