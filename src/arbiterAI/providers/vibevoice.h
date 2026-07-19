#ifndef _arbiterAI_providers_vibevoice_h_
#define _arbiterAI_providers_vibevoice_h_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"

#include <atomic>
#include <mutex>
#include <string>

namespace arbiterAI
{

/**
 * @class VibeVoice
 * @brief Local text-to-speech provider backed by vibevoice.cpp
 *
 * Handles models configured with `"provider": "vibevoice"` and
 * `"mode": "speech"`. The vibevoice C API (vv_capi_*) is a global singleton, so
 * this provider serializes all synthesis and reloads the engine when a different
 * model is requested.
 *
 * Model resolution: `file_path` is the TTS gguf; the tokenizer is expected at
 * `tokenizer.gguf` and the voice at `voice.gguf` (or `voice-<name>.gguf`, or an
 * explicit path in the request `voice`) alongside it. Output is a 24 kHz mono
 * WAV, returned as bytes in SpeechResponse::audio.
 */
class VibeVoice : public BaseProvider
{
public:
    VibeVoice();
    ~VibeVoice() override;

    ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;

    ErrorCode synthesizeSpeech(const SpeechRequest &request,
        const ModelInfo &model,
        SpeechResponse &response) override;

private:
    std::string resolveModelPath(const ModelInfo &model);

    std::mutex m_mutex;                 ///< Serializes the global vv_capi_* engine
    std::string m_loadedModel;          ///< Path of the currently loaded TTS model ("" = none)
    std::atomic<uint64_t> m_tempCounter{ 0 }; ///< Unique-name counter for temp WAV files
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_vibevoice_h_
