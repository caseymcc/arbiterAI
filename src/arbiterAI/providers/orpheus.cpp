#include "arbiterAI/providers/orpheus.h"

#include <spdlog/spdlog.h>
#include <dlfcn.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace arbiterAI
{

Orpheus::Orpheus()
    : BaseProvider("orpheus")
{
}

Orpheus::~Orpheus()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_handle)
    {
        dlclose(m_handle);
        m_handle = nullptr;
        m_ttsFn = nullptr;
    }
}

ErrorCode Orpheus::completion(const CompletionRequest &request,
    const ModelInfo &model,
    CompletionResponse &response)
{
    return ErrorCode::NotImplemented;
}

ErrorCode Orpheus::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    return ErrorCode::NotImplemented;
}

ErrorCode Orpheus::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    return ErrorCode::NotImplemented;
}

std::string Orpheus::resolveModelPath(const ModelInfo &model)
{
    return resolveDownloadableModelFile(model);
}

Orpheus::OrpheusTtsFn Orpheus::acquireEngine()
{
    if(m_ttsFn)
        return m_ttsFn;
    if(m_loadAttempted)
        return nullptr; // don't retry a known-failed load every request

    m_loadAttempted = true;

    // Engine library is isolated (RTLD_LOCAL) so its bundled/forked ggml does
    // not clash with the llama provider's ggml in this process.
    const char *envLib=std::getenv("ARBITER_ORPHEUS_ENGINE");
    std::string libPath=(envLib && *envLib) ? envLib : "liborpheus_engine.so";

    m_handle=dlopen(libPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if(!m_handle)
    {
        spdlog::warn("Orpheus provider: could not load engine library '{}': {}",
            libPath, dlerror());
        return nullptr;
    }

    dlerror(); // clear
    m_ttsFn=reinterpret_cast<OrpheusTtsFn>(dlsym(m_handle, "arbiter_orpheus_tts"));
    const char *symErr=dlerror();
    if(symErr || !m_ttsFn)
    {
        spdlog::warn("Orpheus provider: engine '{}' missing 'arbiter_orpheus_tts': {}",
            libPath, symErr ? symErr : "not found");
        dlclose(m_handle);
        m_handle=nullptr;
        m_ttsFn=nullptr;
        return nullptr;
    }

    return m_ttsFn;
}

ErrorCode Orpheus::synthesizeSpeech(const SpeechRequest &request,
    const ModelInfo &model,
    SpeechResponse &response)
{
    std::string modelPath=resolveModelPath(model);
    if(modelPath.empty())
    {
        spdlog::warn("Orpheus provider: no model file configured for '{}'", model.model);
        return ErrorCode::ModelNotFound;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    OrpheusTtsFn tts=acquireEngine();
    if(!tts)
        return ErrorCode::ModelLoadError;

    namespace fs=std::filesystem;
    fs::path tmpWav=fs::temp_directory_path()
        /("arbiter_orpheus_"+std::to_string(m_tempCounter.fetch_add(1))+".wav");

    int rc=tts(modelPath.c_str(), request.input.c_str(), request.voice.c_str(),
        tmpWav.string().c_str());
    if(rc!=0)
    {
        spdlog::warn("Orpheus provider: engine returned {} for '{}'", rc, model.model);
        std::error_code ec;
        fs::remove(tmpWav, ec);
        return ErrorCode::GenerationError;
    }

    std::ifstream in(tmpWav, std::ios::binary);
    if(!in)
    {
        spdlog::warn("Orpheus provider: engine produced no WAV at '{}'", tmpWav.string());
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
    response.provider="orpheus";

    return ErrorCode::Success;
}

} // namespace arbiterAI
