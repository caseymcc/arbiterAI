#ifndef _arbiterAI_providers_llamaInterface_h_
#define _arbiterAI_providers_llamaInterface_h_

#include "arbiterAI/modelDownloader.h"

#include <future>
#include <map>
#include <memory>
#include <vector>

#include <llama.h>

namespace arbiterAI
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

class LlamaInterface
{
public:
    static LlamaInterface &instance();

    ~LlamaInterface();

    void setModels(const std::vector<ModelInfo> &models);

    ErrorCode completion(const std::string &prompt, std::string &result);

    ErrorCode streamingCompletion(const std::string &prompt,
        std::function<void(const std::string &)> callback);

    ErrorCode getEmbeddings(const std::string &input,
        std::vector<float> &embedding, int &tokens_used);
    ErrorCode getEmbeddings(const std::vector<std::string> &input,
        std::vector<float> &embedding, int &tokens_used);

    bool isLoaded(const std::string &modelName) const;
    ErrorCode loadModel(const std::string &modelName);

private:
    LlamaInterface();

    void initialize();
    bool isModelDownloaded(const LlamaModelInfo &modelInfo) const;
    void downloadModel(LlamaModelInfo &modelInfo);
    DownloadStatus getDownloadStatus(const std::string &modelName, std::string &error);

    bool m_initialized{ false };
    std::optional<ModelInfo> m_modelInfo;
    ModelDownloader m_downloader;
    llama_model *m_model=nullptr;
    llama_context *m_ctx=nullptr;
    std::vector<LlamaModelInfo> m_llamaModels;
    std::map<std::string, std::future<void>> m_downloadFutures;
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_llamaInterface_h_