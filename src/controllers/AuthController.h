#pragma once

#include <drogon/HttpController.h>

#include <memory>

#include "auth/TokenService.h"

namespace posture
{

// Not auto created because it needs the TokenService; main() registers the
// instance.
class AuthController : public drogon::HttpController<AuthController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::device, "/api/v1/auth/device", drogon::Post);
    METHOD_LIST_END

    explicit AuthController(std::shared_ptr<const TokenService> tokens);

    // Body: {"device_id":"<install UUID>"}. Creates the user on first contact,
    // refreshes last_seen_at otherwise, and returns a fresh access token.
    void device(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);

  private:
    std::shared_ptr<const TokenService> tokens_;
};

}  // namespace posture
