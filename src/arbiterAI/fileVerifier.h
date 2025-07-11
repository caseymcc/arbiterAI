#ifndef _arbiterAI_fileVerifier_h_
#define _arbiterAI_fileVerifier_h_

#include <string>

namespace arbiterAI
{

class IFileVerifier
{
public:
    virtual ~IFileVerifier() = default;
    virtual bool verifyFile(const std::string &filePath, const std::string &expectedHash) = 0;
};

class FileVerifier : public IFileVerifier
{
public:
    bool verifyFile(const std::string &filePath, const std::string &expectedHash) override;
};

} // namespace arbiterAI

#endif // _arbiterAI_fileVerifier_h_