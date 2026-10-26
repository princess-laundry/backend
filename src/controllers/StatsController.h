#pragma once

#include <drogon/HttpController.h>

namespace posture
{

class StatsController : public drogon::HttpController<StatsController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(StatsController::daily,
                  "/api/v1/stats/daily",
                  drogon::Get,
                  "posture::JwtFilter");
    METHOD_LIST_END

    // ?from=YYYY-MM-DD&to=YYYY-MM-DD, both inclusive Seoul dates. One entry
    // per day in the range that has at least one session.
    void daily(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace posture
