#include "hermesaxiom/modelDownloader.h"
#include "hermesaxiom/modelManager.h"
#include <picosha2.h>
#include <cpr/cpr.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <spdlog/spdlog.h>

namespace hermesaxiom
{

std::future<bool> ModelDownloader::downloadModel(const std::string &downloadUrl, const std::string &filePathStr, const std::optional<std::string> &fileHash, const std::optional<std::string> &minVersion, const std::optional<std::string> &maxVersion)
{
    return std::async(std::launch::async, [this, downloadUrl, filePathStr, fileHash, minVersion, maxVersion]()
        {
            // Check version compatibility first
            if(minVersion||maxVersion)
            {
                std::string clientVersion="1.0.0"; // TODO: Get from build config
                if((minVersion&&clientVersion<*minVersion)||
                    (maxVersion&&clientVersion>*maxVersion))
                {
                    spdlog::error("Version mismatch: client {} not compatible with model requirements (min: {}, max: {})",
                        clientVersion, minVersion?*minVersion:"none", maxVersion?*maxVersion:"none");
                    return false;
                }
            }

            std::filesystem::path filePath(filePathStr);
            if(std::filesystem::exists(filePath))
            {
                if(fileHash&&verifyFile(filePath.string(), *fileHash))
                {
                    spdlog::info("Model already exists and is verified: {}", filePath.string());
                    return true;
                }
            }

            spdlog::info("Downloading model from {} to {}", downloadUrl, filePath.string());
            cpr::Response r=cpr::Get(cpr::Url{ downloadUrl });
            if(r.status_code!=200)
            {
                spdlog::error("Failed to download model. Status code: {}", r.status_code);
                return false;
            }

            try
            {
                std::filesystem::create_directories(filePath.parent_path());
                std::ofstream outFile(filePath, std::ios::binary);
                outFile.write(r.text.c_str(), r.text.length());
                outFile.close();
            }
            catch(const std::filesystem::filesystem_error &e)
            {
                spdlog::error("Failed to write model to file: {}. Error: {}", filePath.string(), e.what());
                return false;
            }

            if(fileHash)
            {
                if(verifyFile(filePath.string(), *fileHash))
                {
                    spdlog::info("Model downloaded and verified successfully: {}", filePath.string());
                    return true;
                }
                else
                {
                    spdlog::error("SHA256 verification failed for: {}", filePath.string());
                    return false;
                }
            }

            return true;
        });
}

bool ModelDownloader::verifyFile(const std::string &filePath, const std::string &expectedHash)
{
    std::ifstream file(filePath, std::ios::binary);
    if(!file.is_open())
    {
        return false;
    }

    std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(file), {});
    std::string hash_hex_str;
    picosha2::hash256_hex_string(buffer, hash_hex_str);

    return hash_hex_str==expectedHash;
}

} // namespace hermesaxiom