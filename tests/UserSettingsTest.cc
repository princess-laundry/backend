#include "settings/UserSettings.h"

#include <gtest/gtest.h>
#include <json/reader.h>

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{

using posture::parseSettingsUpdate;

Json::Value parse(const std::string &text)
{
    Json::Value value;
    std::istringstream in(text);
    std::string errors;
    Json::CharReaderBuilder builder;
    if (!Json::parseFromStream(builder, in, &value, &errors))
        throw std::runtime_error("bad test JSON: " + errors);
    return value;
}

std::string error(const std::string &text)
{
    try
    {
        parseSettingsUpdate(parse(text));
        return "";
    }
    catch (const std::invalid_argument &e)
    {
        return e.what();
    }
}

}  // namespace

TEST(UserSettingsTest, ReadsModeAndBaseline)
{
    const auto update = parseSettingsUpdate(parse(
        R"({"alert_mode":"VIBRATE","calib_ratio":0.12,"calib_angle":-3.5})"));
    EXPECT_EQ(update.alertMode, "VIBRATE");
    ASSERT_TRUE(update.calibRatio);
    ASSERT_TRUE(update.calibAngle);
    EXPECT_DOUBLE_EQ(*update.calibRatio, 0.12);
    EXPECT_DOUBLE_EQ(*update.calibAngle, -3.5);
}

TEST(UserSettingsTest, NullsClearTheBaseline)
{
    const auto update = parseSettingsUpdate(
        parse(R"({"alert_mode":"OFF","calib_ratio":null,"calib_angle":null})"));
    EXPECT_EQ(update.alertMode, "OFF");
    EXPECT_FALSE(update.calibRatio);
    EXPECT_FALSE(update.calibAngle);
}

TEST(UserSettingsTest, AcceptsWholeNumbersAndIgnoresUnknownKeys)
{
    const auto update = parseSettingsUpdate(parse(
        R"({"alert_mode":"SOUND","calib_ratio":0,"calib_angle":12,"theme":"dark"})"));
    EXPECT_DOUBLE_EQ(*update.calibRatio, 0.0);
    EXPECT_DOUBLE_EQ(*update.calibAngle, 12.0);
}

TEST(UserSettingsTest, RejectsUnknownOrMissingAlertMode)
{
    const std::string message = "alert_mode must be one of SOUND, VIBRATE, OFF";
    EXPECT_EQ(error(R"({"alert_mode":"LOUD","calib_ratio":null,"calib_angle":null})"), message);
    EXPECT_EQ(error(R"({"alert_mode":"sound","calib_ratio":null,"calib_angle":null})"), message);
    EXPECT_EQ(error(R"({"calib_ratio":null,"calib_angle":null})"), message);
    EXPECT_EQ(error(R"({"alert_mode":1,"calib_ratio":null,"calib_angle":null})"), message);
}

TEST(UserSettingsTest, RequiresBothCalibrationKeys)
{
    EXPECT_EQ(error(R"({"alert_mode":"OFF","calib_angle":null})"),
              "calib_ratio is required (use null to clear it)");
    EXPECT_EQ(error(R"({"alert_mode":"OFF","calib_ratio":null})"),
              "calib_angle is required (use null to clear it)");
}

TEST(UserSettingsTest, RejectsHalfABaseline)
{
    EXPECT_EQ(error(R"({"alert_mode":"OFF","calib_ratio":0.1,"calib_angle":null})"),
              "calib_ratio and calib_angle must both be numbers or both be null");
}

TEST(UserSettingsTest, RejectsNonNumbersAndOutOfRangeValues)
{
    EXPECT_EQ(error(R"({"alert_mode":"OFF","calib_ratio":"0.1","calib_angle":1})"),
              "calib_ratio must be a number or null");
    EXPECT_EQ(error(R"({"alert_mode":"OFF","calib_ratio":2.5,"calib_angle":1})"),
              "calib_ratio must be between -2 and 2");
    EXPECT_EQ(error(R"({"alert_mode":"OFF","calib_ratio":0.1,"calib_angle":-91})"),
              "calib_angle must be between -90 and 90");
    EXPECT_EQ(error(R"({"alert_mode":"OFF","calib_ratio":-2,"calib_angle":90})"), "");
}

TEST(UserSettingsTest, RejectsNonObjectBodies)
{
    EXPECT_EQ(error("[]"), "body must be a JSON object");
    EXPECT_EQ(error("null"), "body must be a JSON object");
}
