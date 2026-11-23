#include "fetchDataSource.h"

#include <emscripten/fetch.h>

#include <cstring>

namespace arbiterAI
{
namespace dashboard
{

namespace
{

/// What a request keeps until its reply: the handler, and the body, which
/// emscripten_fetch reads from until it is sent.
struct Pending
{
    std::string body;
    ResponseHandler handler;
};

void finished(emscripten_fetch_t *fetch)
{
    Pending *pending=static_cast<Pending *>(fetch->userData);
    std::string body(fetch->data?fetch->data:"", static_cast<size_t>(fetch->numBytes));
    Response response=makeResponse(fetch->status, body);

    emscripten_fetch_close(fetch);
    if(pending->handler)
        pending->handler(response);
    delete pending;
}

}//namespace

void FetchDataSource::getJson(const std::string &path, ResponseHandler handler)
{
    send("GET", path, std::string(), std::move(handler));
}

void FetchDataSource::postJson(const std::string &path, const nlohmann::json &body, ResponseHandler handler)
{
    send("POST", path, body.dump(), std::move(handler));
}

void FetchDataSource::putJson(const std::string &path, const nlohmann::json &body, ResponseHandler handler)
{
    send("PUT", path, body.dump(), std::move(handler));
}

void FetchDataSource::del(const std::string &path, ResponseHandler handler)
{
    send("DELETE", path, std::string(), std::move(handler));
}

void FetchDataSource::send(const char *method, const std::string &path, std::string body, ResponseHandler handler)
{
    static const char *jsonHeaders[]={"Content-Type", "application/json", "Accept", "application/json", nullptr};
    static const char *acceptHeaders[]={"Accept", "application/json", nullptr};

    Pending *pending=new Pending{std::move(body), std::move(handler)};

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strncpy(attr.requestMethod, method, sizeof(attr.requestMethod)-1);
    attr.attributes=EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.userData=pending;
    attr.onsuccess=finished;
    attr.onerror=finished;
    if(pending->body.empty())
    {
        attr.requestHeaders=acceptHeaders;
    }
    else
    {
        attr.requestHeaders=jsonHeaders;
        attr.requestData=pending->body.data();
        attr.requestDataSize=pending->body.size();
    }
    emscripten_fetch(&attr, path.c_str());
}

}//namespace dashboard
}//namespace arbiterAI
