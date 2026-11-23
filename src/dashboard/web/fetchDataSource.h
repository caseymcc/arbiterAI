#ifndef _ARBITERAI_DASHBOARD_FETCHDATASOURCE_H_
#define _ARBITERAI_DASHBOARD_FETCHDATASOURCE_H_

#include "data/dataSource.h"

namespace arbiterAI
{
namespace dashboard
{

/// The page's own server, through emscripten_fetch (same origin, so the paths
/// are used as they are). Replies arrive on the browser's event loop.
class FetchDataSource: public DataSource
{
public:
    void getJson(const std::string &path, ResponseHandler handler) override;
    void postJson(const std::string &path, const nlohmann::json &body, ResponseHandler handler) override;
    void putJson(const std::string &path, const nlohmann::json &body, ResponseHandler handler) override;
    void del(const std::string &path, ResponseHandler handler) override;

private:
    void send(const char *method, const std::string &path, std::string body, ResponseHandler handler);
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_FETCHDATASOURCE_H_
