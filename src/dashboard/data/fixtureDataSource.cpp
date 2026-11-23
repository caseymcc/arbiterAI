#include "fixtureDataSource.h"

namespace arbiterAI
{
namespace dashboard
{

void FixtureDataSource::setJson(const std::string &path, const nlohmann::json &json, const std::string &method)
{
    m_replies[method+" "+path]=Reply{200, json.dump()};
}

void FixtureDataSource::setFailure(const std::string &path, int status, const std::string &message, const std::string &method)
{
    m_replies[method+" "+path]=Reply{status, status==0?std::string():nlohmann::json{{"error", message}}.dump()};
}

void FixtureDataSource::clear(const std::string &path, const std::string &method)
{
    m_replies.erase(method+" "+path);
}

size_t FixtureDataSource::count(const std::string &path, const std::string &method) const
{
    size_t total=0;

    for(const Request &request:m_requests)
    {
        if(request.method==method && request.path==path)
            ++total;
    }
    return total;
}

void FixtureDataSource::getJson(const std::string &path, ResponseHandler handler)
{
    answer("GET", path, nlohmann::json(), handler);
}

void FixtureDataSource::postJson(const std::string &path, const nlohmann::json &body, ResponseHandler handler)
{
    answer("POST", path, body, handler);
}

void FixtureDataSource::del(const std::string &path, ResponseHandler handler)
{
    answer("DELETE", path, nlohmann::json(), handler);
}

void FixtureDataSource::answer(const std::string &method, const std::string &path, const nlohmann::json &body, const ResponseHandler &handler)
{
    m_requests.push_back(Request{method, path, body});

    auto reply=m_replies.find(method+" "+path);
    Response response=reply==m_replies.end()
        ?makeResponse(404, nlohmann::json{{"error", "no fixture for "+method+" "+path}}.dump())
        :makeResponse(reply->second.status, reply->second.body);
    if(handler)
        handler(response);
}

}//namespace dashboard
}//namespace arbiterAI
