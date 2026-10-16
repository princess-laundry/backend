#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace posture
{

struct DatabaseConfig
{
    std::string host = "localhost";
    std::uint16_t port = 5432;
    std::string name = "posture";
    std::string user = "posture";
    std::string password;
    std::size_t poolSize = 4;
};

struct AppConfig
{
    std::string listenAddress = "0.0.0.0";
    std::uint16_t port = 8080;
    // 0 lets Drogon use one IO thread per CPU core.
    std::size_t threads = 0;
    std::string logLevel = "INFO";
    DatabaseConfig db;

    using Lookup =
        std::function<std::optional<std::string>(const std::string &key)>;

    // Builds the config from a key lookup. Throws std::invalid_argument when
    // a value cannot be parsed or a required key is missing.
    static AppConfig fromLookup(const Lookup &lookup);

    // Same as fromLookup, backed by the process environment.
    static AppConfig fromEnvironment();
};

}  // namespace posture
