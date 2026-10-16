#include "controllers/ConfigController.h"

#include <drogon/drogon.h>

#include "http/ErrorResponse.h"

namespace posture
{

void ConfigController::active(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    auto respond =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT version, t1_ratio, t2_angle, stable_sec, analysis_fps "
        "FROM threshold_configs WHERE is_active",
        [respond](const drogon::orm::Result &result) {
            if (result.empty())
            {
                (*respond)(makeErrorResponse(drogon::k404NotFound,
                                             "CONFIG_NOT_FOUND",
                                             "no active threshold config"));
                return;
            }

            // REAL columns arrive as their shortest text form ("0.8"), so
            // reading them as double keeps the value the row was written with.
            const auto &row = result[0];
            Json::Value body;
            body["version"] = row["version"].as<int>();
            body["t1_ratio"] = row["t1_ratio"].as<double>();
            body["t2_angle"] = row["t2_angle"].as<double>();
            body["stable_sec"] = row["stable_sec"].as<double>();
            body["analysis_fps"] = row["analysis_fps"].as<int>();
            (*respond)(drogon::HttpResponse::newHttpJsonResponse(body));
        },
        [respond](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "config query failed: " << e.base().what();
            (*respond)(makeErrorResponse(drogon::k503ServiceUnavailable,
                                         "DATABASE_UNAVAILABLE",
                                         "database is not reachable"));
        });
}

}  // namespace posture
