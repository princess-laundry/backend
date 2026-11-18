#include "controllers/UserController.h"

#include <drogon/drogon.h>

#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "filters/JwtFilter.h"
#include "http/ErrorResponse.h"
#include "settings/UserSettings.h"
#include "time/IsoTime.h"

namespace posture
{
namespace
{

using Callback = std::function<void(const drogon::HttpResponsePtr &)>;
using CallbackPtr = std::shared_ptr<Callback>;

// Columns every settings response is built from, given a row of users u
// LEFT JOINed or upserted with user_settings s.
const std::string kSettingsColumns =
    "s.alert_mode, s.calib_ratio, s.calib_angle, "
    "(extract(epoch FROM s.updated_at) * 1000000)::bigint AS updated_us";

drogon::HttpResponsePtr databaseUnavailable()
{
    return makeErrorResponse(drogon::k503ServiceUnavailable,
                             "DATABASE_UNAVAILABLE",
                             "database is not reachable");
}

// A valid token can outlive its user, for example after DELETE /users/me.
drogon::HttpResponsePtr userGone()
{
    auto resp = makeErrorResponse(
        drogon::k401Unauthorized, "UNAUTHORIZED", "user no longer exists");
    resp->addHeader("WWW-Authenticate", "Bearer");
    return resp;
}

Json::Value settingsJson(const drogon::orm::Row &row)
{
    const auto real = [&row](const char *column) {
        return row[column].isNull() ? Json::Value()
                                    : Json::Value(row[column].as<double>());
    };

    Json::Value body;
    body["alert_mode"] = row["alert_mode"].isNull()
                             ? std::string(kDefaultAlertMode)
                             : row["alert_mode"].as<std::string>();
    body["calib_ratio"] = real("calib_ratio");
    body["calib_angle"] = real("calib_angle");
    body["updated_at"] =
        row["updated_us"].isNull()
            ? Json::Value()
            : Json::Value(
                  formatSeoulTimestamp(row["updated_us"].as<std::int64_t>()));
    return body;
}

// Shortest text that reads back as the same double, or "" for null. The SQL
// turns "" into NULL, which keeps a single statement for both cases.
std::string realParameter(const std::optional<double> &value)
{
    if (!value)
        return "";
    char buf[32];
    const auto result = std::to_chars(buf, buf + sizeof buf, *value);
    return std::string(buf, result.ptr);
}

}  // namespace

void UserController::removeMe(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    auto respond =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "DELETE FROM users WHERE id = $1",
        [respond](const drogon::orm::Result &) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k204NoContent);
            (*respond)(resp);
        },
        [respond](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "user delete failed: " << e.base().what();
            (*respond)(makeErrorResponse(drogon::k503ServiceUnavailable,
                                         "DATABASE_UNAVAILABLE",
                                         "database is not reachable"));
        },
        JwtFilter::userId(req));
}

void UserController::getSettings(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    auto respond = std::make_shared<Callback>(std::move(callback));
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT " + kSettingsColumns +
            " FROM users u LEFT JOIN user_settings s ON s.user_id = u.id "
            "WHERE u.id = $1",
        [respond](const drogon::orm::Result &result) {
            if (result.empty())
            {
                (*respond)(userGone());
                return;
            }
            (*respond)(
                drogon::HttpResponse::newHttpJsonResponse(settingsJson(result[0])));
        },
        [respond](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "settings lookup failed: " << e.base().what();
            (*respond)(databaseUnavailable());
        },
        JwtFilter::userId(req));
}

void UserController::putSettings(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    const auto json = req->getJsonObject();
    if (!json)
    {
        callback(makeErrorResponse(
            drogon::k400BadRequest, "INVALID_REQUEST", "body must be a JSON object"));
        return;
    }

    SettingsUpdate update;
    try
    {
        update = parseSettingsUpdate(*json);
    }
    catch (const std::invalid_argument &e)
    {
        callback(makeErrorResponse(
            drogon::k400BadRequest, "INVALID_REQUEST", e.what()));
        return;
    }

    auto respond = std::make_shared<Callback>(std::move(callback));
    auto db = drogon::app().getDbClient();
    // Selecting from users makes the upsert a no-op for a deleted user, so an
    // empty result means the token belongs to nobody.
    db->execSqlAsync(
        "INSERT INTO user_settings AS s "
        "(user_id, alert_mode, calib_ratio, calib_angle, updated_at) "
        "SELECT id, $2, NULLIF($3, '')::real, NULLIF($4, '')::real, now() "
        "FROM users WHERE id = $1 "
        "ON CONFLICT (user_id) DO UPDATE SET "
        "alert_mode = EXCLUDED.alert_mode, "
        "calib_ratio = EXCLUDED.calib_ratio, "
        "calib_angle = EXCLUDED.calib_angle, "
        "updated_at = EXCLUDED.updated_at "
        "RETURNING " + kSettingsColumns,
        [respond](const drogon::orm::Result &result) {
            if (result.empty())
            {
                (*respond)(userGone());
                return;
            }
            (*respond)(
                drogon::HttpResponse::newHttpJsonResponse(settingsJson(result[0])));
        },
        [respond](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "settings upsert failed: " << e.base().what();
            (*respond)(databaseUnavailable());
        },
        JwtFilter::userId(req),
        update.alertMode,
        realParameter(update.calibRatio),
        realParameter(update.calibAngle));
}

}  // namespace posture
