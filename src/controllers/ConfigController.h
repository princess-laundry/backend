#pragma once

#include <drogon/HttpController.h>

namespace posture
{

class ConfigController : public drogon::HttpController<ConfigController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConfigController::active,
                  "/api/v1/config",
                  drogon::Get,
                  "posture::JwtFilter");
    METHOD_LIST_END

    // Returns the active threshold config the app classifies with.
    void active(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace posture
