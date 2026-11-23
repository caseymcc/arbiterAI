#ifndef _ARBITERAI_SERVER_DASHBOARDASSETTABLE_H_
#define _ARBITERAI_SERVER_DASHBOARDASSETTABLE_H_

#include <cstddef>
#include <cstring>

namespace arbiterAI
{
namespace server
{

/// A file of the web dashboard embedded in the server (cmake/embedDashboardAssets.cmake).
struct DashboardAsset {
    const char *name;
    const char *mimeType;
    const unsigned char *data;
    size_t size;
    const unsigned char *gzipData;
    size_t gzipSize;
    /// quoted, as sent in the ETag header
    const char *etag;
};

/// The embedded files; none (count 0) in a build without the web dashboard.
const DashboardAsset *dashboardAssets(size_t &count);

/// The embedded file \p name ("shell.html", "app.js", ...), or null.
inline const DashboardAsset *findDashboardAsset(const char *name)
{
    size_t count=0;
    const DashboardAsset *assets=dashboardAssets(count);

    for(size_t i=0; i<count; ++i)
    {
        if(std::strcmp(name, assets[i].name)==0)
            return &assets[i];
    }
    return nullptr;
}

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_DASHBOARDASSETTABLE_H_
