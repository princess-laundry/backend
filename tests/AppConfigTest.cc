#include "config/AppConfig.h"

#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>

namespace
{

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

}  // namespace

TEST(AppConfigTest, UsesDefaultsWhenOnlyPasswordIsSet)
{
    const auto config =
        posture::AppConfig::fromLookup(lookupFrom({{"DB_PASSWORD", "secret"}}));

    EXPECT_EQ(config.listenAddress, "0.0.0.0");
    EXPECT_EQ(config.port, 8080);
    EXPECT_EQ(config.threads, 0u);
    EXPECT_EQ(config.logLevel, "INFO");
    EXPECT_EQ(config.db.host, "localhost");
    EXPECT_EQ(config.db.port, 5432);
    EXPECT_EQ(config.db.name, "posture");
    EXPECT_EQ(config.db.user, "posture");
    EXPECT_EQ(config.db.password, "secret");
    EXPECT_EQ(config.db.poolSize, 4u);
}

TEST(AppConfigTest, ReadsEveryKey)
{
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
}

TEST(AppConfigTest, RequiresDatabasePassword)
{
    EXPECT_THROW(posture::AppConfig::fromLookup(lookupFrom({})),
                 std::invalid_argument);
    EXPECT_THROW(posture::AppConfig::fromLookup(lookupFrom({{"DB_PASSWORD", ""}})),
                 std::invalid_argument);
}

TEST(AppConfigTest, RejectsInvalidPorts)
{
    for (const char *port : {"0", "65536", "-1", "80a", ""})
    {
        EXPECT_THROW(posture::AppConfig::fromLookup(
                         lookupFrom({{"DB_PASSWORD", "pw"}, {"APP_PORT", port}})),
                     std::invalid_argument)
            << "port value: '" << port << "'";
    }
}

TEST(AppConfigTest, RejectsZeroPoolSize)
{
    EXPECT_THROW(posture::AppConfig::fromLookup(
                     lookupFrom({{"DB_PASSWORD", "pw"}, {"DB_POOL_SIZE", "0"}})),
                 std::invalid_argument);
}

TEST(AppConfigTest, RejectsUnknownLogLevel)
{
    EXPECT_THROW(posture::AppConfig::fromLookup(
                     lookupFrom({{"DB_PASSWORD", "pw"}, {"APP_LOG_LEVEL", "LOUD"}})),
                 std::invalid_argument);
}
