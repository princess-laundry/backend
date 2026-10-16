#pragma once

#include <drogon/HttpController.h>

namespace posture
{

class HealthController : public drogon::HttpController<HealthController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HealthController::check, "/api/v1/health", drogon::Get);
    METHOD_LIST_END

    // Reports 200 when the server can reach PostgreSQL, 503 otherwise.
    void check(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace posture
