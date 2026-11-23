#ifndef _ARBITERAI_SERVER_DASHBOARDASSETTABLE_H_
#define _ARBITERAI_SERVER_DASHBOARDASSETTABLE_H_

#include <cstddef>

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

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_DASHBOARDASSETTABLE_H_
