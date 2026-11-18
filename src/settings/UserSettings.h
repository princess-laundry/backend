#pragma once

#include <json/value.h>

#include <optional>
#include <string>

namespace posture
{

// A validated PUT /api/v1/users/me/settings body.
struct SettingsUpdate
{
    // SOUND, VIBRATE or OFF, matching the user_settings CHECK constraint.
    std::string alertMode;
    // The personal upright baseline from the app's 5 second calibration:
    // calib_ratio is Calibration.headRatio and calib_angle is
    // Calibration.torsoAngle in degrees. Either both are set or neither is.
    std::optional<double> calibRatio;
    std::optional<double> calibAngle;
};

// Value of alert_mode before the user has saved any settings.
constexpr const char *kDefaultAlertMode = "SOUND";

// Validates an update body and returns it in typed form. Throws
// std::invalid_argument with a message that names the offending field.
//
// alert_mode is required. calib_ratio and calib_angle are required keys whose
// value is a number or null; both null clears the baseline. The ratio is
// signed (head behind the torso axis is negative) and must lie in [-2, 2];
// the angle is signed too and must lie in [-90, 90]. Unknown members are
// ignored.
SettingsUpdate parseSettingsUpdate(const Json::Value &body);

}  // namespace posture
