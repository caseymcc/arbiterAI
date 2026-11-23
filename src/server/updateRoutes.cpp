#include "updateRoutes.h"

#include <atomic>
#include <mutex>

namespace arbiterAI
{
namespace server
{

namespace
{

std::atomic<UpdateManager *> g_updateManager{nullptr};

std::mutex g_applyMutex;
ApplyContext g_applyContext;
std::shared_ptr<const Environment> g_environment;

nlohmann::json withApplyStatus(nlohmann::json status)
{
    status.update(applyStatusJson());
    return status;
}

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

void setApplyContext(const ApplyContext &context, std::shared_ptr<const Environment> env)
{
    std::lock_guard<std::mutex> lock(g_applyMutex);
    g_applyContext=context;
    g_environment=std::move(env);
}

nlohmann::json applyStatusJson()
{
    std::lock_guard<std::mutex> lock(g_applyMutex);
    if(!g_environment)
    {
        return {
            {"can_apply", false},
            {"apply_blocked_reason", "the installer is not set up"},
            {"supervisor", nullptr}
        };
    }

    ApplyCheck check=checkCanApply(g_applyContext, *g_environment);
    return {
        {"can_apply", check.canApply},
        {"apply_blocked_reason", check.canApply?nlohmann::json(nullptr):nlohmann::json(check.blockedReason)},
        {"supervisor", supervisorJson(detectSupervisor(*g_environment))}
    };
}

void handleGetUpdate(const httplib::Request &, httplib::Response &res)
{
    UpdateManager *manager=nullptr;
    if(!requireManager(res, manager))
        return;
    res.set_content(withApplyStatus(manager->statusJson()).dump(), "application/json");
}

void handleCheckUpdate(const httplib::Request &, httplib::Response &res)
{
    UpdateManager *manager=nullptr;
    if(!requireManager(res, manager))
        return;
    res.set_content(withApplyStatus(manager->checkNow()).dump(), "application/json");
}

void registerUpdateRoutes(httplib::Server &server)
{
    server.Get("/api/update", handleGetUpdate);
    server.Post("/api/update/check", handleCheckUpdate);
}

} // namespace server
} // namespace arbiterAI
