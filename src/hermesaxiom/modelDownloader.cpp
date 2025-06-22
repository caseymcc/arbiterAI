#include "hermesaxiom/modelDownloader.h"
#include "hermesaxiom/modelManager.h"
#include <picosha2.h>
#include <cpr/cpr.h>
#include <fstream>
#include <filesystem>
#include <iostream>

namespace hermesaxiom
{

    std::future<bool> ModelDownloader::downloadModel(const std::string& downloadUrl, const std::string& filePathStr, const std::optional<std::string>& fileHash)
{
    return std::async(std::launch::async, [this, downloadUrl, filePathStr, fileHash]() {
        std::filesystem::path filePath(filePathStr);
        if (std::filesystem::exists(filePath)) {
            if (fileHash && verifyFile(filePath.string(), *fileHash)) {
                return true;
            }
        }

        cpr::Response r = cpr::Get(cpr::Url{downloadUrl});
        if (r.status_code != 200) {
            return false;
        }

        std::filesystem::create_directories(filePath.parent_path());
        std::ofstream outFile(filePath, std::ios::binary);
        outFile.write(r.text.c_str(), r.text.length());
        outFile.close();

        if (fileHash) {
            return verifyFile(filePath.string(), *fileHash);
        }

        return true;
    });
}

bool ModelDownloader::verifyFile(const std::string& filePath, const std::string& expectedHash)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(file), {});
    std::string hash_hex_str;
    picosha2::hash256_hex_string(buffer, hash_hex_str);

    return hash_hex_str == expectedHash;
}

} // namespace hermesaxiom