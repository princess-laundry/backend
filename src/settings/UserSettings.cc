#include "settings/UserSettings.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace posture
{
namespace
{

constexpr double kMaxCalibRatio = 2.0;
constexpr double kMaxCalibAngle = 90.0;

[[noreturn]] void reject(const std::string &message)
{
    throw std::invalid_argument(message);
}

const Json::Value &member(const Json::Value &object, const char *key)
{
    static const Json::Value kNull;
    const auto *found = object.find(key, key + std::char_traits<char>::length(key));
    return found ? *found : kNull;
}

bool hasMember(const Json::Value &object, const char *key)
{
    return object.find(key, key + std::char_traits<char>::length(key)) != nullptr;
}

std::optional<double> optionalBounded(const Json::Value &body,
                                      const char *key,
                                      double limit)
{
    if (!hasMember(body, key))
        reject(std::string(key) + " is required (use null to clear it)");

    const auto &value = member(body, key);
    if (value.isNull())
        return std::nullopt;
    if (!value.isNumeric())
        reject(std::string(key) + " must be a number or null");

    const double number = value.asDouble();
    if (!std::isfinite(number) || std::fabs(number) > limit)
    {
        reject(std::string(key) + " must be between " +
               std::to_string(static_cast<int>(-limit)) + " and " +
               std::to_string(static_cast<int>(limit)));
    }
    return number;
}

}  // namespace

SettingsUpdate parseSettingsUpdate(const Json::Value &body)
{
    if (!body.isObject())
        reject("body must be a JSON object");

    static const std::array<const char *, 3> kModes = {"SOUND", "VIBRATE", "OFF"};

    SettingsUpdate update;
    const auto &mode = member(body, "alert_mode");
    bool known = false;
    if (mode.isString())
    {
        for (const auto *candidate : kModes)
            known = known || mode.asString() == candidate;
    }
    if (!known)
        reject("alert_mode must be one of SOUND, VIBRATE, OFF");
    update.alertMode = mode.asString();

    update.calibRatio = optionalBounded(body, "calib_ratio", kMaxCalibRatio);
    update.calibAngle = optionalBounded(body, "calib_angle", kMaxCalibAngle);
    if (update.calibRatio.has_value() != update.calibAngle.has_value())
        reject("calib_ratio and calib_angle must both be numbers or both be null");
    return update;
}

}  // namespace posture
