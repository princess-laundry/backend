#include "config/AppConfig.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace posture
{
namespace
{

constexpr std::uint64_t kMinJwtTtlSec = 60;
constexpr std::uint64_t kMaxJwtTtlSec = 30 * 24 * 60 * 60;

std::uint64_t parseUnsigned(const std::string &key,
                            const std::string &value,
                            std::uint64_t max)
{
    const bool allDigits =
        !value.empty() &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        });
    if (!allDigits)
    {
        throw std::invalid_argument(key + " must be a non-negative integer, got '" +
                                    value + "'");
    }

    std::uint64_t parsed = 0;
    try
    {
        parsed = std::stoull(value);
    }
    catch (const std::out_of_range &)
    {
        throw std::invalid_argument(key + " is out of range");
    }
    if (parsed > max)
    {
        throw std::invalid_argument(key + " must be at most " +
                                    std::to_string(max));
    }
    return parsed;
}

std::uint16_t parsePort(const std::string &key, const std::string &value)
{
    const auto port = parseUnsigned(key, value, 65535);
    if (port == 0)
    {
        throw std::invalid_argument(key + " must not be 0");
    }
    return static_cast<std::uint16_t>(port);
}

std::string normalizeLogLevel(const std::string &value)
{
    static const std::array<const char *, 6> kLevels = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

    std::string upper = value;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    for (const auto *level : kLevels)
    {
        if (upper == level)
        {
            return upper;
        }
    }
    throw std::invalid_argument("APP_LOG_LEVEL must be one of TRACE, DEBUG, "
                                "INFO, WARN, ERROR, FATAL");
}

}  // namespace

AppConfig AppConfig::fromLookup(const Lookup &lookup)
{
    AppConfig config;

    if (auto v = lookup("APP_LISTEN_ADDRESS"))
        config.listenAddress = *v;
    if (auto v = lookup("APP_PORT"))
        config.port = parsePort("APP_PORT", *v);
    if (auto v = lookup("APP_THREADS"))
        config.threads = parseUnsigned("APP_THREADS", *v, 256);
    if (auto v = lookup("APP_LOG_LEVEL"))
        config.logLevel = normalizeLogLevel(*v);

    if (auto v = lookup("DB_HOST"))
        config.db.host = *v;
    if (auto v = lookup("DB_PORT"))
        config.db.port = parsePort("DB_PORT", *v);
    if (auto v = lookup("DB_NAME"))
        config.db.name = *v;
    if (auto v = lookup("DB_USER"))
        config.db.user = *v;
    if (auto v = lookup("DB_POOL_SIZE"))
    {
        config.db.poolSize = parseUnsigned("DB_POOL_SIZE", *v, 64);
        if (config.db.poolSize == 0)
            throw std::invalid_argument("DB_POOL_SIZE must not be 0");
    }

    auto password = lookup("DB_PASSWORD");
    if (!password || password->empty())
    {
        throw std::invalid_argument("DB_PASSWORD is required");
    }
    config.db.password = *password;

    if (auto v = lookup("JWT_TTL_SEC"))
    {
        config.jwt.ttlSec = parseUnsigned("JWT_TTL_SEC", *v, kMaxJwtTtlSec);
        if (config.jwt.ttlSec < kMinJwtTtlSec)
        {
            throw std::invalid_argument("JWT_TTL_SEC must be at least " +
                                        std::to_string(kMinJwtTtlSec));
        }
    }

    auto secret = lookup("JWT_SECRET");
    if (!secret || secret->empty())
    {
        throw std::invalid_argument("JWT_SECRET is required");
    }
    if (secret->size() < JwtConfig::kMinSecretLength)
    {
        throw std::invalid_argument(
            "JWT_SECRET must be at least " +
            std::to_string(JwtConfig::kMinSecretLength) + " characters");
    }
    config.jwt.secret = *secret;

    return config;
}

AppConfig AppConfig::fromEnvironment()
{
    return fromLookup([](const std::string &key) -> std::optional<std::string> {
        const char *value = std::getenv(key.c_str());
        if (value == nullptr)
            return std::nullopt;
        return std::string(value);
    });
}

}  // namespace posture
