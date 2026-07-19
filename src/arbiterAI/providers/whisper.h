#ifndef _arbiterAI_providers_whisper_h_
#define _arbiterAI_providers_whisper_h_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

struct whisper_context;

namespace arbiterAI
{

/**
 * @class Whisper
 * @brief Local speech-to-text provider backed by whisper.cpp
 *
 * Handles models configured with `"provider": "whisper"` and
 * `"mode": "transcription"`. The GGUF/bin model path is taken from the model
 * config (`file_path`, or the primary variant download filename resolved under
 * the storage root). Loaded whisper contexts are cached per model file path and
 * reused across requests.
 *
 * First-cut audio support: 16 kHz mono 16-bit PCM WAV. Other formats / sample
 * rates return ErrorCode::InvalidRequest (transcode before calling). Resampling
 * and compressed-format decoding are follow-ups.
 */
class Whisper : public BaseProvider
{
public:
    Whisper();
    ~Whisper() override;

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

private:
    /// Resolve the on-disk whisper model file for a model config.
    /// Returns empty string if no usable path is configured.
    std::string resolveModelPath(const ModelInfo &model);

    /// Get (loading on first use) a whisper context for the given model file.
    /// Returns nullptr on load failure. Thread-safe.
    whisper_context *acquireContext(const std::string &modelPath);

    /// Decode raw audio file bytes into 16 kHz mono f32 PCM samples.
    /// Only uncompressed 16-bit PCM WAV at 16 kHz mono is supported for now.
    static ErrorCode decodeAudio(const std::vector<uint8_t> &bytes,
        std::vector<float> &samplesOut, std::string &error);

    std::mutex m_mutex;              ///< Guards the context cache
    std::mutex m_inferenceMutex;     ///< Serializes whisper_full (not reentrant on a shared ctx) — v1 limitation
    std::map<std::string, whisper_context *> m_contexts;
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_whisper_h_
