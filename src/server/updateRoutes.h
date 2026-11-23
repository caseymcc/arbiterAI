#ifndef _ARBITERAI_SERVER_UPDATEROUTES_H_
#define _ARBITERAI_SERVER_UPDATEROUTES_H_

#include "updateManager.h"
#include "installEnvironment.h"

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

/// GET /api/update: UpdateManager::statusJson() and applyStatusJson().
void handleGetUpdate(const httplib::Request &req, httplib::Response &res);

/// POST /api/update/check: a check now, then the same object as GET.
void handleCheckUpdate(const httplib::Request &req, httplib::Response &res);

void registerUpdateRoutes(httplib::Server &server);

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_UPDATEROUTES_H_
