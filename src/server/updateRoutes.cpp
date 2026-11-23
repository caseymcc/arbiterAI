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

std::atomic<UpdateApplier *> g_updateApplier{nullptr};
std::atomic<AutoUpdater *> g_autoUpdater{nullptr};

std::mutex g_applyMutex;
ApplyContext g_applyContext;
std::shared_ptr<const Environment> g_environment;

nlohmann::json withApplyStatus(nlohmann::json status)
{
    status.update(applyStatusJson());
    UpdateApplier *applier=g_updateApplier.load();
    if(applier)
        status.update(applier->statusJson());
    AutoUpdater *autoUpdater=g_autoUpdater.load();
    status["auto"]=autoUpdater
        ?autoUpdater->statusJson()
        :nlohmann::json{{"enabled", false}, {"status", "off"}, {"next_attempt", nullptr}};
    return status;
}

void sendError(httplib::Response &res, int status, const std::string &message, const char *code)
{
    nlohmann::json error={{"error", {
        {"message", message},
        {"type", status == 503?"server_error":"invalid_request_error"},
        {"param", nullptr},
        {"code", code}
    }}};
    res.status=status;
    res.set_content(error.dump(), "application/json");
}

const char *startCode(UpdateApplier::Start start)
{
    switch(start)
    {
    case UpdateApplier::Start::Busy:
        return "update_running";
    case UpdateApplier::Start::Blocked:
        return "apply_blocked";
    case UpdateApplier::Start::NotLatest:
        return "not_latest";
    case UpdateApplier::Start::NoAsset:
        return "no_asset";
    case UpdateApplier::Start::NoPrevious:
        return "no_previous";
    default:
        return nullptr;
    }
}

void answerStart(httplib::Response &res, UpdateApplier::Start start, const std::string &message, UpdateManager *manager)
{
    if(start != UpdateApplier::Start::Started)
    {
        sendError(res, applyStartStatus(start), message, startCode(start));
        return;
    }
    res.status=202;
    res.set_content(withApplyStatus(manager->statusJson()).dump(), "application/json");
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

void setUpdateApplier(UpdateApplier *applier)
{
    g_updateApplier.store(applier);
}

void setAutoUpdater(AutoUpdater *updater)
{
    g_autoUpdater.store(updater);
}

ApplyCheck currentApplyCheck()
{
    std::lock_guard<std::mutex> lock(g_applyMutex);
    if(!g_environment)
        return ApplyCheck{false, "the installer is not set up"};
    return checkCanApply(g_applyContext, *g_environment);
}

Supervisor currentSupervisor()
{
    std::lock_guard<std::mutex> lock(g_applyMutex);
    return g_environment?detectSupervisor(*g_environment):Supervisor::None;
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

void handleApplyUpdate(const httplib::Request &req, httplib::Response &res)
{
    UpdateManager *manager=nullptr;
    if(!requireManager(res, manager))
        return;
    UpdateApplier *applier=g_updateApplier.load();
    if(!applier)
    {
        sendError(res, 503, "The installer is not running", nullptr);
        return;
    }

    nlohmann::json body=nlohmann::json::parse(req.body, nullptr, false);
    if(!body.is_object() || !body.contains("version") || !body["version"].is_string())
    {
        sendError(res, 400, "the body must be {\"version\": \"X.Y.Z\"}", "invalid_value");
        return;
    }

    std::string message;
    UpdateApplier::Start start=applier->apply(body["version"].get<std::string>(), manager->status().latest,
        currentApplyCheck(), message);
    answerStart(res, start, message, manager);
}

void handleRollbackUpdate(const httplib::Request &, httplib::Response &res)
{
    UpdateManager *manager=nullptr;
    if(!requireManager(res, manager))
        return;
    UpdateApplier *applier=g_updateApplier.load();
    if(!applier)
    {
        sendError(res, 503, "The installer is not running", nullptr);
        return;
    }

    std::string message;
    UpdateApplier::Start start=applier->rollback(currentApplyCheck(), message);
    answerStart(res, start, message, manager);
}

void registerUpdateRoutes(httplib::Server &server)
{
    server.Post("/api/update/apply", handleApplyUpdate);
    server.Post("/api/update/rollback", handleRollbackUpdate);
    server.Get("/api/update", handleGetUpdate);
    server.Post("/api/update/check", handleCheckUpdate);
}

} // namespace server
} // namespace arbiterAI
