#include "controllers/UserController.h"

#include <drogon/drogon.h>

#include <memory>

#include "filters/JwtFilter.h"
#include "http/ErrorResponse.h"

namespace posture
{

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

}  // namespace posture
