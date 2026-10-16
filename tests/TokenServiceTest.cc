#include "auth/TokenService.h"

#include <gtest/gtest.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/defaults.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using posture::TokenCheck;
using posture::TokenService;

const std::string kSecret = "0123456789abcdef0123456789abcdef";
const std::string kUserId = "7c9e6679-7425-40de-944b-e07fc1f90ae7";
constexpr std::chrono::seconds kTtl{3600};
// Whole seconds, because JWT dates have second resolution.
const TokenService::Clock::time_point kNow{std::chrono::seconds(1800000000)};

TokenService service()
{
    return TokenService(kSecret, kTtl);
}

std::vector<std::string> splitToken(const std::string &token)
{
    std::vector<std::string> parts;
    std::string::size_type start = 0;
    for (;;)
    {
        const auto dot = token.find('.', start);
        parts.push_back(token.substr(start, dot - start));
        if (dot == std::string::npos)
            return parts;
        start = dot + 1;
    }
}

// A correctly signed token with every claim the service expects. Tests drop
// or change one claim at a time to make sure each check is enforced.
auto validBuilder()
{
    return jwt::create()
        .set_type("JWT")
        .set_issuer(TokenService::kIssuer)
        .set_subject(kUserId)
        .set_issued_at(kNow)
        .set_expires_at(kNow + kTtl);
}

}  // namespace

TEST(TokenServiceTest, IssuedTokenVerifies)
{
    const auto tokens = service();
    const auto check = tokens.verify(tokens.issue(kUserId, kNow), kNow);

    EXPECT_EQ(check.status, TokenCheck::Status::Valid);
    EXPECT_EQ(check.userId, kUserId);
}

TEST(TokenServiceTest, IssuedTokenCarriesExpectedClaims)
{
    const auto decoded = jwt::decode(service().issue(kUserId, kNow));

    EXPECT_EQ(decoded.get_algorithm(), "HS256");
    EXPECT_EQ(decoded.get_type(), "JWT");
    EXPECT_EQ(decoded.get_issuer(), TokenService::kIssuer);
    EXPECT_EQ(decoded.get_subject(), kUserId);
    EXPECT_EQ(decoded.get_issued_at(), kNow);
    EXPECT_EQ(decoded.get_expires_at(), kNow + kTtl);
}

TEST(TokenServiceTest, ExpiresAfterTtl)
{
    const auto tokens = service();
    const auto token = tokens.issue(kUserId, kNow);

    EXPECT_EQ(tokens.verify(token, kNow + kTtl).status, TokenCheck::Status::Valid);

    const auto late = tokens.verify(token, kNow + kTtl + std::chrono::seconds(1));
    EXPECT_EQ(late.status, TokenCheck::Status::Expired);
    EXPECT_TRUE(late.userId.empty());
}

TEST(TokenServiceTest, RejectsTokenIssuedInTheFuture)
{
    const auto tokens = service();
    const auto token = tokens.issue(kUserId, kNow + std::chrono::hours(1));

    EXPECT_EQ(tokens.verify(token, kNow).status, TokenCheck::Status::Invalid);
}

TEST(TokenServiceTest, RejectsTokenSignedWithAnotherSecret)
{
    const TokenService other(std::string(32, 'x'), kTtl);
    const auto token = other.issue(kUserId, kNow);

    EXPECT_EQ(service().verify(token, kNow).status, TokenCheck::Status::Invalid);
}

TEST(TokenServiceTest, RejectsTamperedPayload)
{
    const auto tokens = service();
    const auto original = splitToken(tokens.issue(kUserId, kNow));
    const auto forged = splitToken(
        tokens.issue("00000000-0000-0000-0000-000000000000", kNow));
    ASSERT_EQ(original.size(), 3u);
    ASSERT_EQ(forged.size(), 3u);

    // Someone else's payload with our signature.
    const auto spliced = original[0] + "." + forged[1] + "." + original[2];
    EXPECT_EQ(tokens.verify(spliced, kNow).status, TokenCheck::Status::Invalid);
}

TEST(TokenServiceTest, RejectsUnsignedToken)
{
    const auto token = validBuilder().sign(jwt::algorithm::none{});

    EXPECT_EQ(service().verify(token, kNow).status, TokenCheck::Status::Invalid);
}

TEST(TokenServiceTest, RejectsOtherHmacAlgorithm)
{
    const auto token = validBuilder().sign(jwt::algorithm::hs512{kSecret});

    EXPECT_EQ(service().verify(token, kNow).status, TokenCheck::Status::Invalid);
}

TEST(TokenServiceTest, RequiresExpiry)
{
    const auto token = jwt::create()
                           .set_type("JWT")
                           .set_issuer(TokenService::kIssuer)
                           .set_subject(kUserId)
                           .set_issued_at(kNow)
                           .sign(jwt::algorithm::hs256{kSecret});

    EXPECT_EQ(service().verify(token, kNow).status, TokenCheck::Status::Invalid);
}

TEST(TokenServiceTest, RequiresOurIssuer)
{
    const auto token = validBuilder()
                           .set_issuer("someone-else")
                           .sign(jwt::algorithm::hs256{kSecret});

    EXPECT_EQ(service().verify(token, kNow).status, TokenCheck::Status::Invalid);
}

TEST(TokenServiceTest, RequiresUuidSubject)
{
    const auto token =
        validBuilder().set_subject("admin").sign(jwt::algorithm::hs256{kSecret});

    EXPECT_EQ(service().verify(token, kNow).status, TokenCheck::Status::Invalid);
}

TEST(TokenServiceTest, RejectsMalformedTokens)
{
    const auto tokens = service();
    for (const char *token : {"", "abc", "a.b.c", "..", "e30.e30.", "!!!.???.***"})
    {
        EXPECT_EQ(tokens.verify(token, kNow).status, TokenCheck::Status::Invalid)
            << "token: '" << token << "'";
    }
}

TEST(TokenServiceTest, RefusesToIssueForNonUuidSubject)
{
    EXPECT_THROW(service().issue("admin", kNow), std::invalid_argument);
}

TEST(TokenServiceTest, RejectsUnusableSettings)
{
    EXPECT_THROW(TokenService("", kTtl), std::invalid_argument);
    EXPECT_THROW(TokenService(kSecret, std::chrono::seconds(0)),
                 std::invalid_argument);
}

TEST(BearerTokenTest, ExtractsToken)
{
    EXPECT_EQ(posture::bearerToken("Bearer abc.def.ghi"), "abc.def.ghi");
    EXPECT_EQ(posture::bearerToken("bearer abc"), "abc");
    EXPECT_EQ(posture::bearerToken("BEARER   abc  "), "abc");
}

TEST(BearerTokenTest, RejectsOtherHeaders)
{
    for (const char *header :
         {"", "Bearer", "Bearer ", "Bearer    ", "Bearerabc", "Basic abc",
          "Bearer abc def", "Token abc"})
    {
        EXPECT_FALSE(posture::bearerToken(header).has_value())
            << "header: '" << header << "'";
    }
}
