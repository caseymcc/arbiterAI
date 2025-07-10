#ifndef _hermesaxiom_fileVerifier_h_
#define _hermesaxiom_fileVerifier_h_

#include <string>

namespace hermesaxiom
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

} // namespace hermesaxiom

#endif // _hermesaxiom_fileVerifier_h_