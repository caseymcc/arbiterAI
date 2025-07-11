#ifndef _ARBITERAI_CONFIGDOWNLOADER_H_
#define _ARBITERAI_CONFIGDOWNLOADER_H_

#include <string>
#include <filesystem>

namespace arbiterAI
{
class ConfigDownloader
{
public:
    void initialize(const std::string &repoUrl, const std::filesystem::path &localPath, const std::string &version="main");
    const std::filesystem::path &getLocalPath() const;

private:
    std::filesystem::path m_localPath;
};
}

#endif//_ARBITERAI_CONFIGDOWNLOADER_H_