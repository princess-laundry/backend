#include "controllers/HealthController.h"

#include <drogon/drogon.h>

#ifndef POSTURE_VERSION
#define POSTURE_VERSION "dev"
#endif

namespace posture
{

void HealthController::check(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    auto respond =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT 1",
        [respond](const drogon::orm::Result &) {
            Json::Value body;
            body["status"] = "ok";
            body["version"] = POSTURE_VERSION;
            body["database"] = "ok";
            (*respond)(drogon::HttpResponse::newHttpJsonResponse(body));
        },
        [respond](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "health check query failed: " << e.base().what();

            Json::Value body;
            body["error"]["code"] = "DATABASE_UNAVAILABLE";
            body["error"]["message"] = "database is not reachable";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
            resp->setStatusCode(drogon::k503ServiceUnavailable);
            (*respond)(resp);
        });
}

}  // namespace posture
