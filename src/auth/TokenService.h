#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace posture
{

struct TokenCheck
{
    enum class Status
    {
        Valid,
        Expired,
        Invalid
    };

    Status status = Status::Invalid;
    // Set only when status is Valid.
    std::string userId;
};

// Issues and verifies the HS256 access tokens handed out by
// POST /api/v1/auth/device. The subject is the users.id UUID.
class TokenService
{
  public:
    using Clock = std::chrono::system_clock;

    static constexpr const char *kIssuer = "posture-backend";

    TokenService(std::string secret, std::chrono::seconds ttl);

    std::chrono::seconds ttl() const
    {
        return ttl_;
    }

    // Throws std::invalid_argument when userId is not a UUID.
    std::string issue(const std::string &userId, Clock::time_point now) const;
    std::string issue(const std::string &userId) const
    {
        return issue(userId, Clock::now());
    }

    // Never throws. A token is valid only when it is signed with our secret
    // using HS256, has our issuer, carries a UUID subject and an expiry, and
    // that expiry has not passed.
    TokenCheck verify(const std::string &token, Clock::time_point now) const;
    TokenCheck verify(const std::string &token) const
    {
        return verify(token, Clock::now());
    }

  private:
    std::string secret_;
    std::chrono::seconds ttl_;
};

// Extracts the token from an Authorization header value of the form
// "Bearer <token>". The scheme is matched case insensitively.
std::optional<std::string> bearerToken(const std::string &authorization);

}  // namespace posture
