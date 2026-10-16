#include "auth/TokenService.h"

#include <jwt-cpp/traits/open-source-parsers-jsoncpp/defaults.h>

#include <cctype>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "auth/DeviceId.h"

namespace posture
{
namespace
{

using JsonTraits = jwt::traits::open_source_parsers_jsoncpp;

// jwt-cpp reads the current time from a clock object. This one returns the
// time passed to verify() so tests can check expiry without sleeping.
struct FixedClock
{
    jwt::date time;

    jwt::date now() const
    {
        return time;
    }
};

}  // namespace

TokenService::TokenService(std::string secret, std::chrono::seconds ttl)
    : secret_(std::move(secret)), ttl_(ttl)
{
    if (secret_.empty())
        throw std::invalid_argument("token secret must not be empty");
    if (ttl_.count() <= 0)
        throw std::invalid_argument("token lifetime must be positive");
}

std::string TokenService::issue(const std::string &userId,
                                Clock::time_point now) const
{
    if (!isUuid(userId))
        throw std::invalid_argument("token subject must be a UUID");

    return jwt::create<JsonTraits>()
        .set_type("JWT")
        .set_issuer(kIssuer)
        .set_subject(userId)
        .set_issued_at(now)
        .set_expires_at(now + ttl_)
        .sign(jwt::algorithm::hs256{secret_});
}

TokenCheck TokenService::verify(const std::string &token,
                                Clock::time_point now) const
{
    TokenCheck check;
    try
    {
        const jwt::decoded_jwt<JsonTraits> decoded(token);
        // jwt-cpp skips the expiry check when exp is absent, so require it.
        if (!decoded.has_expires_at() || !decoded.has_subject())
            return check;

        std::error_code ec;
        jwt::verify<FixedClock, JsonTraits>(FixedClock{now})
            .allow_algorithm(jwt::algorithm::hs256{secret_})
            .with_issuer(kIssuer)
            .verify(decoded, ec);
        if (ec)
        {
            // Claims are only checked after the signature, so an expired
            // result always belongs to a token we issued ourselves.
            if (ec == jwt::error::token_verification_error::token_expired &&
                now > decoded.get_expires_at())
            {
                check.status = TokenCheck::Status::Expired;
            }
            return check;
        }

        auto subject = decoded.get_subject();
        if (!isUuid(subject))
            return check;

        check.status = TokenCheck::Status::Valid;
        check.userId = std::move(subject);
    }
    catch (const std::exception &)
    {
        // Malformed base64, JSON or claim types.
        check = TokenCheck{};
    }
    return check;
}

std::optional<std::string> bearerToken(const std::string &authorization)
{
    static const std::string kScheme = "bearer";

    if (authorization.size() <= kScheme.size() ||
        authorization[kScheme.size()] != ' ')
    {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < kScheme.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(authorization[i]);
        if (std::tolower(c) != kScheme[i])
            return std::nullopt;
    }

    const auto begin = authorization.find_first_not_of(' ', kScheme.size());
    if (begin == std::string::npos)
        return std::nullopt;
    const auto end = authorization.find_last_not_of(' ');
    auto token = authorization.substr(begin, end - begin + 1);
    if (token.find(' ') != std::string::npos)
        return std::nullopt;
    return token;
}

}  // namespace posture
