#include "config/AppConfig.h"

#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>

namespace
{

const std::string kSecret(32, 's');

posture::AppConfig::Lookup lookupFrom(std::map<std::string, std::string> values)
{
    return [values = std::move(values)](const std::string &key)
               -> std::optional<std::string> {
        auto it = values.find(key);
        if (it == values.end())
            return std::nullopt;
        return it->second;
    };
}

// The required keys plus the given overrides.
posture::AppConfig::Lookup withRequired(
    std::map<std::string, std::string> overrides = {})
{
    std::map<std::string, std::string> values = {
        {"DB_PASSWORD", "pw"},
        {"JWT_SECRET", kSecret},
    };
    for (auto &entry : overrides)
        values[entry.first] = entry.second;
    return lookupFrom(std::move(values));
}

}  // namespace

TEST(AppConfigTest, UsesDefaultsWhenOnlyRequiredKeysAreSet)
{
    const auto config = posture::AppConfig::fromLookup(withRequired());

    EXPECT_EQ(config.listenAddress, "0.0.0.0");
    EXPECT_EQ(config.port, 8080);
    EXPECT_EQ(config.threads, 0u);
    EXPECT_EQ(config.logLevel, "INFO");
    EXPECT_EQ(config.db.host, "localhost");
    EXPECT_EQ(config.db.port, 5432);
    EXPECT_EQ(config.db.name, "posture");
    EXPECT_EQ(config.db.user, "posture");
    EXPECT_EQ(config.db.password, "pw");
    EXPECT_EQ(config.db.poolSize, 4u);
    EXPECT_EQ(config.jwt.secret, kSecret);
    EXPECT_EQ(config.jwt.ttlSec, 604800u);
}

TEST(AppConfigTest, ReadsEveryKey)
{
    const std::string secret(48, 'k');
    const auto config = posture::AppConfig::fromLookup(lookupFrom({
        {"APP_LISTEN_ADDRESS", "127.0.0.1"},
        {"APP_PORT", "9000"},
        {"APP_THREADS", "2"},
        {"APP_LOG_LEVEL", "debug"},
        {"DB_HOST", "db"},
        {"DB_PORT", "6543"},
        {"DB_NAME", "posture_test"},
        {"DB_USER", "tester"},
        {"DB_PASSWORD", "pw"},
        {"DB_POOL_SIZE", "8"},
        {"JWT_SECRET", secret},
        {"JWT_TTL_SEC", "3600"},
    }));

    EXPECT_EQ(config.listenAddress, "127.0.0.1");
    EXPECT_EQ(config.port, 9000);
    EXPECT_EQ(config.threads, 2u);
    EXPECT_EQ(config.logLevel, "DEBUG");
    EXPECT_EQ(config.db.host, "db");
    EXPECT_EQ(config.db.port, 6543);
    EXPECT_EQ(config.db.name, "posture_test");
    EXPECT_EQ(config.db.user, "tester");
    EXPECT_EQ(config.db.password, "pw");
    EXPECT_EQ(config.db.poolSize, 8u);
    EXPECT_EQ(config.jwt.secret, secret);
    EXPECT_EQ(config.jwt.ttlSec, 3600u);
}

TEST(AppConfigTest, RequiresDatabasePassword)
{
    EXPECT_THROW(
        posture::AppConfig::fromLookup(lookupFrom({{"JWT_SECRET", kSecret}})),
        std::invalid_argument);
    EXPECT_THROW(posture::AppConfig::fromLookup(withRequired({{"DB_PASSWORD", ""}})),
                 std::invalid_argument);
}

TEST(AppConfigTest, RequiresJwtSecret)
{
    EXPECT_THROW(posture::AppConfig::fromLookup(lookupFrom({{"DB_PASSWORD", "pw"}})),
                 std::invalid_argument);
    EXPECT_THROW(posture::AppConfig::fromLookup(withRequired({{"JWT_SECRET", ""}})),
                 std::invalid_argument);
}

TEST(AppConfigTest, RejectsShortJwtSecret)
{
    const std::string almost(posture::JwtConfig::kMinSecretLength - 1, 'x');
    const std::string enough(posture::JwtConfig::kMinSecretLength, 'x');

    EXPECT_THROW(posture::AppConfig::fromLookup(withRequired({{"JWT_SECRET", almost}})),
                 std::invalid_argument);
    EXPECT_EQ(posture::AppConfig::fromLookup(withRequired({{"JWT_SECRET", enough}}))
                  .jwt.secret,
              enough);
}

TEST(AppConfigTest, ChecksJwtTtlRange)
{
    for (const char *ttl : {"0", "59", "2592001", "-1", "1h", ""})
    {
        EXPECT_THROW(posture::AppConfig::fromLookup(withRequired({{"JWT_TTL_SEC", ttl}})),
                     std::invalid_argument)
            << "ttl value: '" << ttl << "'";
    }
    EXPECT_EQ(posture::AppConfig::fromLookup(withRequired({{"JWT_TTL_SEC", "60"}}))
                  .jwt.ttlSec,
              60u);
    EXPECT_EQ(posture::AppConfig::fromLookup(withRequired({{"JWT_TTL_SEC", "2592000"}}))
                  .jwt.ttlSec,
              2592000u);
}

TEST(AppConfigTest, RejectsInvalidPorts)
{
    for (const char *port : {"0", "65536", "-1", "80a", ""})
    {
        EXPECT_THROW(posture::AppConfig::fromLookup(withRequired({{"APP_PORT", port}})),
                     std::invalid_argument)
            << "port value: '" << port << "'";
    }
}

TEST(AppConfigTest, RejectsZeroPoolSize)
{
    EXPECT_THROW(posture::AppConfig::fromLookup(withRequired({{"DB_POOL_SIZE", "0"}})),
                 std::invalid_argument);
}

TEST(AppConfigTest, RejectsUnknownLogLevel)
{
    EXPECT_THROW(posture::AppConfig::fromLookup(withRequired({{"APP_LOG_LEVEL", "LOUD"}})),
                 std::invalid_argument);
}
