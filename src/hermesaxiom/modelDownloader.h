#ifndef _hermesaxiom_modelDownloader_h_
#define _hermesaxiom_modelDownloader_h_

#include "hermesaxiom/modelManager.h"
#include "hermesaxiom/hermesaxiom.h"

#include <string>
#include <future>

namespace hermesaxiom
{

class ModelDownloader
{
public:
    ModelDownloader() = default;

    std::future<bool> downloadModel(const std::string& downloadUrl, const std::string& filePath, const std::optional<std::string>& fileHash);

private:
    bool verifyFile(const std::string& filePath, const std::string& expectedHash);
};

} // namespace hermesaxiom

#endif // _hermesaxiom_modelDownloader_h_