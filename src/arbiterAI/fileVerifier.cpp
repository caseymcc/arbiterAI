#include "arbiterAI/fileVerifier.h"
#include <picosha2.h>
#include <fstream>
#include <vector>

namespace arbiterAI
{

bool FileVerifier::verifyFile(const std::string &filePath, const std::string &expectedHash)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }

    std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(file), {});
    std::string hash_hex_str;
    picosha2::hash256_hex_string(buffer, hash_hex_str);

    return hash_hex_str == expectedHash;
}

} // namespace arbiterAI