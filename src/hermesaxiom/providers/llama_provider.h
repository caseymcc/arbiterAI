#ifndef _hermesaxiom_providers_llama_provider_h_
#define _hermesaxiom_providers_llama_provider_h_

#include "hermesaxiom/modelDownloader.h"
#include "hermesaxiom/providers/base_llm.h"
#include <future>
#include <map>
#include <memory>
#include <vector>

#include <llama.h>

namespace hermesaxiom
{

struct LlamaModelInfo
{
    ModelInfo modelInfo;
    std::optional<std::string> downloadUrl;
    std::optional<std::string> filePath;
    std::optional<std::string> fileHash;
    DownloadStatus downloadStatus{ DownloadStatus::NotStarted };
    std::string downloadError;
};

class LlamaProvider
{
public:
    static LlamaProvider& instance();

    ~LlamaProvider();

    ErrorCode completion(const std::string& prompt, std::string& result);

    ErrorCode streamingCompletion(const std::string& prompt,
        std::function<void(const std::string&)> callback);

    ErrorCode getEmbeddings(const std::string& input,
        std::vector<float>& embedding, int& tokens_used);

    bool isLoaded(const std::string& modelName) const;
    bool loadModel(const std::string& modelName);

private:
    LlamaProvider();


    void initialize();
    void loadLlamaConfig();
    bool isModelDownloaded(const LlamaModelInfo& modelInfo) const;
    void downloadModel(LlamaModelInfo& modelInfo);
    DownloadStatus getDownloadStatus(const std::string& modelName, std::string& error);

    bool m_initialized{ false };
    std::optional<ModelInfo> m_modelInfo;
    ModelDownloader m_downloader;
    llama_model* m_model=nullptr;
    llama_context* m_ctx=nullptr;
    std::vector<LlamaModelInfo> m_llamaModels;
    std::map<std::string, std::future<void>> m_downloadFutures;
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_llama_provider_h_