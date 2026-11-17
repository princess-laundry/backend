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
    METHOD_LIST_END

    // Deletes the caller and, through ON DELETE CASCADE, every session,
    // segment, daily stat and setting they own. 204 even when the user was
    // already gone, so a retried request still succeeds.
    void removeMe(const drogon::HttpRequestPtr &req,
                  std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace posture
