#ifndef _ARBITERAI_DASHBOARD_DATASOURCE_H_
#define _ARBITERAI_DASHBOARD_DATASOURCE_H_

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace arbiterAI
{
namespace dashboard
{

/// What a request to the server came back with.
struct Response {
    /// a 2xx status with a body that parsed (or none)
    bool ok=false;
    /// the HTTP status; 0 when the server could not be reached
    int status=0;
    nlohmann::json json;
    /// why it failed, for the status line or a toast; empty when ok
    std::string error;
};

using ResponseHandler=std::function<void(const Response &response)>;

/// The server's REST API as the dashboard reads it: same-origin paths
/// ("/api/stats"), JSON in and out. Handlers run on the UI thread, later
/// (web) or before the call returns (fixtures).
class DataSource {
public:
    virtual ~DataSource()=default;

    virtual void getJson(const std::string &path, ResponseHandler handler)=0;
    virtual void postJson(const std::string &path, const nlohmann::json &body, ResponseHandler handler)=0;
    virtual void putJson(const std::string &path, const nlohmann::json &body, ResponseHandler handler)=0;
    virtual void del(const std::string &path, ResponseHandler handler)=0;
};

/// A Response from a status and a body: parsed JSON, and the error the server
/// gave ({"error": "..."} or {"error": {"message": "..."}}) or one made from
/// the status.
Response makeResponse(int status, const std::string &body);

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_DATASOURCE_H_
