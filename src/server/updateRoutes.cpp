#include "updateRoutes.h"

#include <atomic>

namespace arbiterAI
{
namespace server
{

namespace
{

std::atomic<UpdateManager *> g_updateManager{nullptr};

bool requireManager(httplib::Response &res, UpdateManager *&manager)
{
    manager=g_updateManager.load();
    if(manager)
        return true;

    nlohmann::json error={{"error", {
        {"message", "The update checker is not running"},
        {"type", "server_error"},
        {"param", nullptr},
        {"code", nullptr}
    }}};
    res.status=503;
    res.set_content(error.dump(), "application/json");
    return false;
}

} // namespace

void setUpdateManager(UpdateManager *manager)
{
    g_updateManager.store(manager);
}

UpdateManager *updateManager()
{
    return g_updateManager.load();
}

void handleGetUpdate(const httplib::Request &, httplib::Response &res)
{
    UpdateManager *manager=nullptr;
    if(!requireManager(res, manager))
        return;
    res.set_content(manager->statusJson().dump(), "application/json");
}

void handleCheckUpdate(const httplib::Request &, httplib::Response &res)
{
    UpdateManager *manager=nullptr;
    if(!requireManager(res, manager))
        return;
    res.set_content(manager->checkNow().dump(), "application/json");
}

void registerUpdateRoutes(httplib::Server &server)
{
    server.Get("/api/update", handleGetUpdate);
    server.Post("/api/update/check", handleCheckUpdate);
}

} // namespace server
} // namespace arbiterAI
