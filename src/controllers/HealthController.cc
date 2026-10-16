#include "controllers/HealthController.h"

#include <drogon/drogon.h>

#include "http/ErrorResponse.h"

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
            (*respond)(makeErrorResponse(drogon::k503ServiceUnavailable,
                                         "DATABASE_UNAVAILABLE",
                                         "database is not reachable"));
        });
}

}  // namespace posture
