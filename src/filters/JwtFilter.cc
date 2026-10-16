#include "filters/JwtFilter.h"

#include <drogon/drogon.h>

#include <utility>

#include "http/ErrorResponse.h"

namespace posture
{
namespace
{

drogon::HttpResponsePtr unauthorized(const std::string &message)
{
    auto resp =
        makeErrorResponse(drogon::k401Unauthorized, "UNAUTHORIZED", message);
    resp->addHeader("WWW-Authenticate", "Bearer");
    return resp;
}

}  // namespace

JwtFilter::JwtFilter(std::shared_ptr<const TokenService> tokens)
    : tokens_(std::move(tokens))
{
}

void JwtFilter::doFilter(const drogon::HttpRequestPtr &req,
                         drogon::FilterCallback &&fcb,
                         drogon::FilterChainCallback &&fccb)
{
    const auto token = bearerToken(req->getHeader("authorization"));
    if (!token)
    {
        fcb(unauthorized("missing bearer token"));
        return;
    }

    auto check = tokens_->verify(*token);
    switch (check.status)
    {
        case TokenCheck::Status::Valid:
            req->attributes()->insert(kUserIdAttribute,
                                      std::move(check.userId));
            fccb();
            return;
        case TokenCheck::Status::Expired:
            fcb(unauthorized("token has expired"));
            return;
        case TokenCheck::Status::Invalid:
            break;
    }
    fcb(unauthorized("invalid token"));
}

const std::string &JwtFilter::userId(const drogon::HttpRequestPtr &req)
{
    return req->attributes()->get<std::string>(kUserIdAttribute);
}

}  // namespace posture
