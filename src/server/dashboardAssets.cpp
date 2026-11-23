#include "dashboardAssets.h"
#include "dashboardAssetTable.h"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace arbiterAI
{
namespace server
{

namespace
{

const char *NOT_BUILT_HTML=R"HTML(<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><title>ArbiterAI Dashboard</title></head>
<body style="font-family: sans-serif; margin: 3em; max-width: 40em;">
<h1>Dashboard not built</h1>
<p>This arbiterAI-server was built without the web dashboard
(<code>ARBITERAI_WEB_DASHBOARD=OFF</code>, or no emsdk found). The REST API under
<code>/api/</code> and <code>/v1/</code> works as usual.</p>
<p>To include the dashboard, build with Emscripten available (<code>EMSDK</code> set),
as the project's Docker image does.</p>
</body>
</html>
)HTML";

std::string trim(const std::string &text)
{
    size_t begin=0;
    size_t end=text.size();

    while(begin<end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    while(end>begin && std::isspace(static_cast<unsigned char>(text[end-1])))
        --end;
    return text.substr(begin, end-begin);
}

std::string toLower(std::string text)
{
    for(char &c:text)
        c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// The comma-separated items of a header value, trimmed.
std::vector<std::string> headerItems(const std::string &value)
{
    std::vector<std::string> items;
    size_t start=0;

    while(start<=value.size())
    {
        size_t comma=value.find(',', start);
        if(comma==std::string::npos)
            comma=value.size();
        std::string item=trim(value.substr(start, comma-start));
        if(!item.empty())
            items.push_back(item);
        start=comma+1;
    }
    return items;
}

const DashboardAsset *findAsset(const std::string &name)
{
    size_t count=0;
    const DashboardAsset *assets=dashboardAssets(count);

    for(size_t i=0; i<count; ++i)
    {
        if(name==assets[i].name)
            return &assets[i];
    }
    return nullptr;
}

} // namespace

bool acceptsGzip(const std::string &acceptEncoding)
{
    for(const std::string &item:headerItems(acceptEncoding))
    {
        size_t semicolon=item.find(';');
        std::string coding=toLower(trim(item.substr(0, semicolon)));

        if(coding!="gzip" && coding!="*")
            continue;

        if(semicolon==std::string::npos)
            return true;

        std::string params=toLower(item.substr(semicolon+1));
        size_t q=params.find("q=");
        if(q==std::string::npos)
            return true;
        return std::atof(params.c_str()+q+2)>0.0;
    }
    return false;
}

bool etagMatches(const std::string &ifNoneMatch, const std::string &etag)
{
    for(std::string item:headerItems(ifNoneMatch))
    {
        if(item=="*")
            return true;
        // If-None-Match compares weakly: W/"x" matches "x"
        if(item.rfind("W/", 0)==0)
            item=item.substr(2);
        if(item==etag)
            return true;
    }
    return false;
}

void serveDashboardAsset(const httplib::Request &req, httplib::Response &res, const std::string &name)
{
    const DashboardAsset *asset=findAsset(name);

    if(!asset)
    {
        res.status=404;
        res.set_content("Not found", "text/plain");
        return;
    }

    res.set_header("ETag", asset->etag);
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Vary", "Accept-Encoding");

    if(etagMatches(req.get_header_value("If-None-Match"), asset->etag))
    {
        res.status=304;
        return;
    }

    const unsigned char *data=asset->data;
    size_t size=asset->size;

    if(acceptsGzip(req.get_header_value("Accept-Encoding")))
    {
        data=asset->gzipData;
        size=asset->gzipSize;
        res.set_header("Content-Encoding", "gzip");
    }

    // A content provider, not set_content: httplib compresses a body of a text
    // or JavaScript type again (brotli for "br"), and the files are already
    // gzipped; a provider of known length is sent as it is.
    res.set_content_provider(size, asset->mimeType,
        [data, size](size_t offset, size_t length, httplib::DataSink &sink)
        {
            if(offset+length>size)
                length=size-offset;
            return sink.write(reinterpret_cast<const char *>(data)+offset, length);
        });
}

void handleDashboardShell(const httplib::Request &req, httplib::Response &res)
{
    if(!findAsset("shell.html"))
    {
        res.set_content(NOT_BUILT_HTML, "text/html; charset=utf-8");
        return;
    }
    serveDashboardAsset(req, res, "shell.html");
}

void registerDashboardRoutes(httplib::Server &server)
{
    server.Get(R"(/dashboard(?:/(?:storage|config|logs)?)?)", handleDashboardShell);
    server.Get(R"(/dashboard/(app\.(?:js|wasm|data)))", [](const httplib::Request &req, httplib::Response &res)
    {
        serveDashboardAsset(req, res, req.matches[1].str());
    });
}

} // namespace server
} // namespace arbiterAI
