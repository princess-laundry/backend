#include "controllers/StatsController.h"

#include <drogon/drogon.h>

#include <cstdint>
#include <memory>
#include <stdexcept>

#include "filters/JwtFilter.h"
#include "http/ErrorResponse.h"
#include "stats/DailyStats.h"

namespace posture
{

void StatsController::daily(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    DateRange range;
    try
    {
        range = parseDateRange(req->getParameter("from"),
                               req->getParameter("to"));
    }
    catch (const std::invalid_argument &e)
    {
        callback(makeErrorResponse(
            drogon::k400BadRequest, "INVALID_REQUEST", e.what()));
        return;
    }

    auto respond =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));
    const auto from = formatLocalDate(range.from);
    const auto to = formatLocalDate(range.to);

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT to_char(stat_date, 'YYYY-MM-DD') AS stat_date, study_sec, "
        "normal_sec, fhp_sec, lean_sec, session_count "
        "FROM daily_stats "
        "WHERE user_id = $1 AND stat_date BETWEEN $2::date AND $3::date "
        "AND session_count > 0 "
        "ORDER BY stat_date",
        [respond, from, to](const drogon::orm::Result &result) {
            Json::Value body;
            body["from"] = from;
            body["to"] = to;
            auto &days = body["days"];
            days = Json::Value(Json::arrayValue);
            for (const auto &row : result)
            {
                const auto normal = row["normal_sec"].as<std::int64_t>();
                const auto fhp = row["fhp_sec"].as<std::int64_t>();
                const auto lean = row["lean_sec"].as<std::int64_t>();
                const auto ratio = normalRatio(normal, fhp, lean);

                Json::Value day;
                day["date"] = row["stat_date"].as<std::string>();
                day["study_sec"] = row["study_sec"].as<int>();
                day["normal_sec"] = static_cast<Json::Int64>(normal);
                day["fhp_sec"] = static_cast<Json::Int64>(fhp);
                day["lean_sec"] = static_cast<Json::Int64>(lean);
                day["session_count"] = row["session_count"].as<int>();
                day["normal_ratio"] =
                    ratio ? Json::Value(*ratio) : Json::Value();
                days.append(day);
            }
            (*respond)(drogon::HttpResponse::newHttpJsonResponse(body));
        },
        [respond](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "daily stats query failed: " << e.base().what();
            (*respond)(makeErrorResponse(drogon::k503ServiceUnavailable,
                                         "DATABASE_UNAVAILABLE",
                                         "database is not reachable"));
        },
        JwtFilter::userId(req),
        from,
        to);
}

}  // namespace posture
