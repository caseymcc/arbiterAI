#ifndef _ARBITERAI_SERVER_UPDATEROUTES_H_
#define _ARBITERAI_SERVER_UPDATEROUTES_H_

#include "updateManager.h"
#include "installEnvironment.h"
#include "updateApplier.h"
#include "autoUpdate.h"

#include <httplib.h>

#include <memory>

namespace arbiterAI
{
namespace server
{

/// The UpdateManager the /api/update routes and the server config use; null
/// (the default) answers 503.
void setUpdateManager(UpdateManager *manager);
UpdateManager *updateManager();

/// What /api/update decides can_apply and supervisor from. Until it is set
/// can_apply is false and supervisor null.
void setApplyContext(const ApplyContext &context, std::shared_ptr<const Environment> env);

/// can_apply, apply_blocked_reason and supervisor.
nlohmann::json applyStatusJson();

/// can_apply and its reason, from the context set with setApplyContext.
ApplyCheck currentApplyCheck();

/// The supervisor of the environment set with setApplyContext.
Supervisor currentSupervisor();

/// The AutoUpdater whose status is GET /api/update's "auto"; null leaves it
/// as {enabled: false, status: "off"}.
void setAutoUpdater(AutoUpdater *updater);

/// The UpdateApplier behind apply and rollback; null (the default) answers 503
/// and leaves its fields out of GET /api/update.
void setUpdateApplier(UpdateApplier *applier);

/// GET /api/update: UpdateManager::statusJson(), applyStatusJson() and
/// UpdateApplier::statusJson().
void handleGetUpdate(const httplib::Request &req, httplib::Response &res);

/// POST /api/update/check: a check now, then the same object as GET.
void handleCheckUpdate(const httplib::Request &req, httplib::Response &res);

/// POST /api/update/apply {"version": "X.Y.Z"}: 202 when the install started,
/// 409 while one runs or when the version is not the latest, 403 when
/// can_apply is false.
void handleApplyUpdate(const httplib::Request &req, httplib::Response &res);

/// POST /api/update/rollback: back to bin.prev, through the same restart.
void handleRollbackUpdate(const httplib::Request &req, httplib::Response &res);

void registerUpdateRoutes(httplib::Server &server);

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_UPDATEROUTES_H_
