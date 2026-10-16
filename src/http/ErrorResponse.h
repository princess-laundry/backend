#pragma once

#include <drogon/HttpResponse.h>

#include <string>

namespace posture
{

// Builds the shared error body {"error":{"code":"...","message":"..."}}.
drogon::HttpResponsePtr makeErrorResponse(drogon::HttpStatusCode status,
                                          const std::string &code,
                                          const std::string &message);

}  // namespace posture
