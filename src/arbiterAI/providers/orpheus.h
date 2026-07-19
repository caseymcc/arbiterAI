#ifndef _arbiterAI_providers_orpheus_h_
#define _arbiterAI_providers_orpheus_h_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"

#include <atomic>
#include <mutex>
#include <string>

namespace arbiterAI
{

/**
 * @class Orpheus
 * @brief Local text-to-speech provider for Orpheus 3B (SNAC-coded speech-LLM)
 *
 * Orpheus is a Llama-3 backbone that emits SNAC audio codes decoded to a 24 kHz
 * waveform. The decode (and, in this design, the whole text->audio pipeline) is
 * handled by chatllm.cpp, which ships a native C++ Orpheus + SNAC decoder.
 *
 * Because chatllm.cpp bundles a *forked* ggml that cannot statically coexist
 * with the ggml linked by the llama provider, chatllm is isolated behind a
 * self-contained engine shared library that we load at runtime with dlopen +
 * RTLD_LOCAL. That library must export the flat C entry point:
 *
 *   int arbiter_orpheus_tts(const char *model_path, const char *text,
 *                           const char *voice, const char *out_wav_path);
 *
 * returning 0 on success and writing a WAV to out_wav_path. (chatllm's own C
 * binding does not expose audio, so this thin shim wraps its internal
 * speech_synthesis — see docs/tasks/multimodal_model_support.md.)
 *
 * The engine library path comes from the env var ARBITER_ORPHEUS_ENGINE, else
 * "liborpheus_engine.so" resolved via the normal loader search path.
 */
class Orpheus : public BaseProvider
{
public:
    Orpheus();
    ~Orpheus() override;

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
    /// Signature of the C entry point the engine library must export.
    typedef int (*OrpheusTtsFn)(const char *modelPath, const char *text,
        const char *voice, const char *outWavPath);

    std::string resolveModelPath(const ModelInfo &model);

    /// dlopen the engine library and resolve the entry point (once). Returns the
    /// function pointer, or nullptr on failure. Thread-safe.
    OrpheusTtsFn acquireEngine();

    std::mutex m_mutex;
    void *m_handle = nullptr;         ///< dlopen handle for the engine library
    OrpheusTtsFn m_ttsFn = nullptr;   ///< resolved arbiter_orpheus_tts
    bool m_loadAttempted = false;
    std::atomic<uint64_t> m_tempCounter{ 0 };
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_orpheus_h_
