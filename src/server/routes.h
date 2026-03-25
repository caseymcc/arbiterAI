#ifndef _ARBITERAI_SERVER_ROUTES_H_
#define _ARBITERAI_SERVER_ROUTES_H_

#include <httplib.h>

namespace arbiterAI
{
namespace server
{

/// Register all route handlers on the given HTTP server.
void registerRoutes(httplib::Server &server);

// ========== Chat Completions (OpenAI-compatible) ==========

void handleChatCompletions(const httplib::Request &req, httplib::Response &res);
void handleListModelsV1(const httplib::Request &req, httplib::Response &res);
void handleGetModelV1(const httplib::Request &req, httplib::Response &res);

// ========== Embeddings (OpenAI-compatible) ==========

void handleEmbeddings(const httplib::Request &req, httplib::Response &res);

// ========== Health ==========

void handleHealth(const httplib::Request &req, httplib::Response &res);

// ========== Model Management ==========

void handleGetModels(const httplib::Request &req, httplib::Response &res);
void handleGetLoadedModels(const httplib::Request &req, httplib::Response &res);
void handleLoadModel(const httplib::Request &req, httplib::Response &res);
void handleUnloadModel(const httplib::Request &req, httplib::Response &res);
void handlePinModel(const httplib::Request &req, httplib::Response &res);
void handleUnpinModel(const httplib::Request &req, httplib::Response &res);
void handleDownloadModel(const httplib::Request &req, httplib::Response &res);
void handleGetDownloadStatus(const httplib::Request &req, httplib::Response &res);

// ========== Telemetry ==========

void handleGetStats(const httplib::Request &req, httplib::Response &res);
void handleGetStatsHistory(const httplib::Request &req, httplib::Response &res);
void handleGetStatsSwaps(const httplib::Request &req, httplib::Response &res);
void handleGetHardware(const httplib::Request &req, httplib::Response &res);

// ========== Dashboard ==========

void handleDashboard(const httplib::Request &req, httplib::Response &res);

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_ROUTES_H_
