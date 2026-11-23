#ifndef _ARBITERAI_SERVER_DASHBOARDASSETS_H_
#define _ARBITERAI_SERVER_DASHBOARDASSETS_H_

#include <httplib.h>

#include <string>

namespace arbiterAI
{
namespace server
{

/// Whether an Accept-Encoding header value allows a gzip body ("gzip" or "*",
/// without q=0).
bool acceptsGzip(const std::string &acceptEncoding);

/// Whether an If-None-Match header value matches \p etag (a quoted ETag; the
/// header may list several, weak ones, or "*").
bool etagMatches(const std::string &ifNoneMatch, const std::string &etag);

/// Sends the embedded dashboard file \p name (dashboardAssetTable.h): its MIME
/// type, gzipped when the client accepts it, its ETag, and 304 when the client
/// already has it. 404 when there is no such file.
void serveDashboardAsset(const httplib::Request &req, httplib::Response &res, const std::string &name);

/// The web dashboard's page (shell.html), or a short "dashboard not built" page
/// in a build without it.
void handleDashboardShell(const httplib::Request &req, httplib::Response &res);

/// The web dashboard's routes: the page at /dashboard and at its tabs'
/// /dashboard/storage, config and logs (the app opens the tab from the path),
/// and its files at /dashboard/app.js, app.wasm and app.data.
void registerDashboardRoutes(httplib::Server &server);

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_DASHBOARDASSETS_H_
