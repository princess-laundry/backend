#include "controllers/AuthController.h"

#include <drogon/drogon.h>

#include <exception>
#include <string>
#include <utility>

#include "auth/DeviceId.h"
#include "http/ErrorResponse.h"

namespace posture
{

AuthController::AuthController(std::shared_ptr<const TokenService> tokens)
    : tokens_(std::move(tokens))
{
}

void AuthController::device(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    // The Android client sends the install id as "device_uuid"; both names
    // are accepted.
    const auto json = req->getJsonObject();
    std::string deviceId;
    if (json && json->isObject())
    {
        for (const char *key : {"device_id", "device_uuid"})
        {
            if ((*json)[key].isString())
            {
                deviceId = (*json)[key].asString();
                break;
            }
        }
    }
    if (!isUuid(deviceId))
    {
        callback(makeErrorResponse(drogon::k400BadRequest,
                                   "INVALID_REQUEST",
                                   "body must be a JSON object with a UUID "
                                   "device_id"));
        return;
    }
    const auto deviceHash = hashDeviceId(deviceId);

    auto respond =
        std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
            std::move(callback));

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "INSERT INTO users (device_hash) VALUES ($1) "
        "ON CONFLICT (device_hash) DO UPDATE SET last_seen_at = now() "
        "RETURNING id",
        [respond, tokens = tokens_](const drogon::orm::Result &result) {
            const auto userId = result[0]["id"].as<std::string>();

            std::string token;
            try
            {
                token = tokens->issue(userId);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR << "failed to issue token: " << e.what();
                (*respond)(makeErrorResponse(drogon::k500InternalServerError,
                                             "INTERNAL_ERROR",
                                             "could not issue token"));
                return;
            }

            Json::Value body;
            body["token"] = token;
            // Same value under the name the Android client reads.
            body["access_token"] = token;
            body["token_type"] = "Bearer";
            body["expires_in"] =
                static_cast<Json::Int64>(tokens->ttl().count());
            body["user_id"] = userId;
            (*respond)(drogon::HttpResponse::newHttpJsonResponse(body));
        },
        [respond](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "device sign in query failed: " << e.base().what();
            (*respond)(makeErrorResponse(drogon::k503ServiceUnavailable,
                                         "DATABASE_UNAVAILABLE",
                                         "database is not reachable"));
        },
        deviceHash);
}

}  // namespace posture
