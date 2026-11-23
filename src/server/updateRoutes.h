#ifndef _ARBITERAI_SERVER_UPDATEROUTES_H_
#define _ARBITERAI_SERVER_UPDATEROUTES_H_

#include "updateManager.h"

#include <httplib.h>

namespace arbiterAI
{
namespace server
{

/// The UpdateManager the /api/update routes and the server config use; null
/// (the default) answers 503.
void setUpdateManager(UpdateManager *manager);
UpdateManager *updateManager();

/// GET /api/update: UpdateManager::statusJson().
void handleGetUpdate(const httplib::Request &req, httplib::Response &res);

/// POST /api/update/check: a check now, then the same object as GET.
void handleCheckUpdate(const httplib::Request &req, httplib::Response &res);

void registerUpdateRoutes(httplib::Server &server);

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_UPDATEROUTES_H_
