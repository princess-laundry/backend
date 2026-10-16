#pragma once

#include <drogon/HttpFilter.h>

#include <memory>
#include <string>

#include "auth/TokenService.h"

namespace posture
{

// Guards protected routes. Requests need "Authorization: Bearer <token>"
// with a token from POST /api/v1/auth/device; anything else gets 401. On
// success the user id is stored in the request attributes.
//
// Not auto created because it needs the TokenService; main() registers the
// instance. Routes refer to it as "posture::JwtFilter".
class JwtFilter : public drogon::HttpFilter<JwtFilter, false>
{
  public:
    static constexpr const char *kUserIdAttribute = "user_id";

    explicit JwtFilter(std::shared_ptr<const TokenService> tokens);

    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;

    // The authenticated user id (UUID) for a request that passed the filter.
    static const std::string &userId(const drogon::HttpRequestPtr &req);

  private:
    std::shared_ptr<const TokenService> tokens_;
};

}  // namespace posture
