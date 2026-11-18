#pragma once

#include <drogon/HttpController.h>

namespace posture
{

class UserController : public drogon::HttpController<UserController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UserController::removeMe,
                  "/api/v1/users/me",
                  drogon::Delete,
                  "posture::JwtFilter");
    ADD_METHOD_TO(UserController::getSettings,
                  "/api/v1/users/me/settings",
                  drogon::Get,
                  "posture::JwtFilter");
    ADD_METHOD_TO(UserController::putSettings,
                  "/api/v1/users/me/settings",
                  drogon::Put,
                  "posture::JwtFilter");
    METHOD_LIST_END

    // Deletes the caller and, through ON DELETE CASCADE, every session,
    // segment, daily stat and setting they own. 204 even when the user was
    // already gone, so a retried request still succeeds.
    void removeMe(const drogon::HttpRequestPtr &req,
                  std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // The caller's alert mode and calibration baseline. Users who never saved
    // settings get the defaults, with updated_at null.
    void getSettings(const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Replaces the caller's settings with the full object in the body and
    // returns what was stored.
    void putSettings(const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace posture
