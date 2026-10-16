#include "http/ErrorResponse.h"

#include <json/value.h>

namespace posture
{

drogon::HttpResponsePtr makeErrorResponse(drogon::HttpStatusCode status,
                                          const std::string &code,
                                          const std::string &message)
{
    Json::Value body;
    body["error"]["code"] = code;
    body["error"]["message"] = message;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    return resp;
}

}  // namespace posture
