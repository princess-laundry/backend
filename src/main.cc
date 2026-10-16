#include <drogon/drogon.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "auth/TokenService.h"
#include "config/AppConfig.h"
#include "controllers/AuthController.h"
#include "filters/JwtFilter.h"

namespace
{

trantor::Logger::LogLevel toLogLevel(const std::string &level)
{
    if (level == "TRACE")
        return trantor::Logger::kTrace;
    if (level == "DEBUG")
        return trantor::Logger::kDebug;
    if (level == "WARN")
        return trantor::Logger::kWarn;
    if (level == "ERROR")
        return trantor::Logger::kError;
    if (level == "FATAL")
        return trantor::Logger::kFatal;
    return trantor::Logger::kInfo;
}

drogon::orm::PostgresConfig toPostgresConfig(const posture::DatabaseConfig &db)
{
    drogon::orm::PostgresConfig pg{};
    pg.host = db.host;
    pg.port = db.port;
    pg.databaseName = db.name;
    pg.username = db.user;
    pg.password = db.password;
    pg.connectionNumber = db.poolSize;
    pg.name = "default";
    pg.isFast = false;
    pg.characterSet = "utf8";
    pg.timeout = 10.0;
    pg.autoBatch = false;
    return pg;
}

}  // namespace

int main()
{
    posture::AppConfig config;
    try
    {
        config = posture::AppConfig::fromEnvironment();
    }
    catch (const std::exception &e)
    {
        std::cerr << "invalid configuration: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    LOG_INFO << "starting posture server on " << config.listenAddress << ':'
             << config.port << ", database " << config.db.host << ':'
             << config.db.port << '/' << config.db.name;

    auto tokens = std::make_shared<const posture::TokenService>(
        config.jwt.secret, std::chrono::seconds(config.jwt.ttlSec));

    drogon::app()
        .setLogLevel(toLogLevel(config.logLevel))
        .addListener(config.listenAddress, config.port)
        .setThreadNum(config.threads)
        // jsoncpp defaults to 17 significant digits, which prints 0.8 as
        // 0.80000000000000004. With 15 (DBL_DIG) any value read from text
        // with up to 15 significant digits prints back unchanged.
        .setFloatPrecisionInJson(15, "significant")
        .addDbClient(toPostgresConfig(config.db))
        .registerFilter(std::make_shared<posture::JwtFilter>(tokens))
        .registerController(std::make_shared<posture::AuthController>(tokens))
        .run();

    return EXIT_SUCCESS;
}
