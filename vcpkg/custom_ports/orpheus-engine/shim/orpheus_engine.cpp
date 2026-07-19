// orpheus_engine.cpp — arbiterAI <-> chatllm.cpp glue.
//
// Built into a self-contained liborpheus_engine.so that statically embeds
// chatllm + its (forked) ggml with hidden visibility, exporting ONLY
// arbiter_orpheus_tts. arbiterAI's Orpheus provider dlopen's this with
// RTLD_LOCAL so the forked ggml never clashes with the host's llama ggml.
//
// Placed in chatllm's src/ so "chat.h" resolves; compiled together with
// chatllm's ${core_files}. See vcpkg/custom_ports/orpheus-engine/README.md.

#include "chat.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{

void put32(std::vector<uint8_t> &b, uint32_t v)
{
    b.push_back(uint8_t(v & 0xFF));
    b.push_back(uint8_t((v >> 8) & 0xFF));
    b.push_back(uint8_t((v >> 16) & 0xFF));
    b.push_back(uint8_t((v >> 24) & 0xFF));
}

void put16(std::vector<uint8_t> &b, uint16_t v)
{
    b.push_back(uint8_t(v & 0xFF));
    b.push_back(uint8_t((v >> 8) & 0xFF));
}

// Write a canonical 16-bit PCM WAV. arbiterAI only reads the bytes back, so any
// valid WAV is fine.
bool write_wav(const char *path, const std::vector<int16_t> &pcm, int sample_rate, int channels)
{
    if (sample_rate <= 0) sample_rate = 24000;
    if (channels <= 0) channels = 1;

    const uint32_t dataBytes = (uint32_t)(pcm.size() * sizeof(int16_t));
    const uint16_t bits = 16;
    const uint32_t byteRate = (uint32_t)sample_rate * channels * bits / 8;

    std::vector<uint8_t> hdr;
    const char *riff = "RIFF"; hdr.insert(hdr.end(), riff, riff + 4);
    put32(hdr, 36 + dataBytes);
    const char *wave = "WAVE"; hdr.insert(hdr.end(), wave, wave + 4);
    const char *fmt = "fmt "; hdr.insert(hdr.end(), fmt, fmt + 4);
    put32(hdr, 16);
    put16(hdr, 1);                                   // PCM
    put16(hdr, (uint16_t)channels);
    put32(hdr, (uint32_t)sample_rate);
    put32(hdr, byteRate);
    put16(hdr, (uint16_t)(channels * bits / 8));     // block align
    put16(hdr, bits);
    const char *data = "data"; hdr.insert(hdr.end(), data, data + 4);
    put32(hdr, dataBytes);

    FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite(hdr.data(), 1, hdr.size(), f);
    if (dataBytes)
        std::fwrite(pcm.data(), 1, dataBytes, f);
    std::fclose(f);
    return true;
}

} // namespace

// chatllm's logging hook is declared extern in src/backend.cpp and defined in
// src/main.cpp, which this engine excludes (we only want the Pipeline, not the
// CLI/binding). Forward warnings/errors to stderr so failures surface in the
// host's logs (journald when run under the server).
void log_internal(int level, const char *text)
{
    if(text && level >= 3) // chatllm: 3=WARN, 4=ERROR (lower are debug/info)
        std::fprintf(stderr, "[chatllm] %s\n", text);
}

extern "C" __attribute__((visibility("default")))
int arbiter_orpheus_tts(const char *model_path,
                        const char *text,
                        const char *voice,
                        const char *out_wav_path)
{
    if (!model_path || !text || !out_wav_path)
        return 10;

    try
    {
        int n_threads = (int)std::thread::hardware_concurrency();
        if (n_threads <= 0) n_threads = 4;

        // Mirrors main.cpp's pipe_args (max_length=-1 => model default,
        // batch_size 4096, f16 cache).
        chatllm::ModelObject::extra_args pipe_args(-1, "", false, n_threads, 4096, "f16", "");
        if (voice && *voice)
            pipe_args.additional["voice"] = voice; // best-effort; ignored if unused

        chatllm::Pipeline pipeline(model_path, pipe_args);
        if (!pipeline.is_loaded())
            return 2;

        int max_length = pipeline.model->get_max_length();

        // Mirrors main.cpp's DEF_GenerationConfig defaults.
        chatllm::GenerationConfig gen_config(
            max_length, /*max_context_length*/ 512, /*do_sample*/ true,
            /*reversed_role*/ false, /*top_k*/ 20, /*top_p*/ 0.7f,
            /*temperature*/ 0.7f, /*num_threads*/ n_threads, /*sampling*/ "top_p",
            /*presence_penalty*/ 0.0f, /*tfs_z*/ 0.95f);

        std::vector<int16_t> audio;
        int sample_rate = 0;
        int channels = 0;
        bool ok = pipeline.speech_synthesis(text, gen_config, audio, sample_rate, channels);
        if (!ok || audio.empty())
            return 3;

        if (!write_wav(out_wav_path, audio, sample_rate, channels))
            return 4;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "[orpheus] exception: %s\n", e.what());
        return 1;
    }
    catch (...)
    {
        std::fprintf(stderr, "[orpheus] unknown exception\n");
        return 1;
    }
}
