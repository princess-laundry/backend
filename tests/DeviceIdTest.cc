#include "auth/DeviceId.h"

#include <gtest/gtest.h>

#include <string>

namespace
{

const std::string kDeviceId = "550e8400-e29b-41d4-a716-446655440000";

}  // namespace

TEST(DeviceIdTest, AcceptsCanonicalUuids)
{
    EXPECT_TRUE(posture::isUuid(kDeviceId));
    EXPECT_TRUE(posture::isUuid("550E8400-E29B-41D4-A716-446655440000"));
    EXPECT_TRUE(posture::isUuid("00000000-0000-0000-0000-000000000000"));
}

TEST(DeviceIdTest, RejectsOtherStrings)
{
    for (const char *value : {"",
                              "550e8400e29b41d4a716446655440000",
                              "550e8400-e29b-41d4-a716-44665544000",
                              "550e8400-e29b-41d4-a716-4466554400000",
                              "550e8400-e29b-41d4-a716_446655440000",
                              "g50e8400-e29b-41d4-a716-446655440000",
                              "{550e8400-e29b-41d4-a716-446655440000}",
                              " 550e8400-e29b-41d4-a716-44665544000"})
    {
        EXPECT_FALSE(posture::isUuid(value)) << "value: '" << value << "'";
    }
}

TEST(DeviceIdTest, HashesWithSha256)
{
    // printf '%s' 550e8400-e29b-41d4-a716-446655440000 | shasum -a 256
    EXPECT_EQ(posture::hashDeviceId(kDeviceId),
              "a3a9e1ed9732cab28868127be00f1ce921acaefdd5c3b23a6e9e0072bd9c1a34");
    // FIPS 180-2 test vector.
    EXPECT_EQ(posture::hashDeviceId("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(DeviceIdTest, HashIgnoresLetterCase)
{
    EXPECT_EQ(posture::hashDeviceId("550E8400-E29B-41D4-A716-446655440000"),
              posture::hashDeviceId(kDeviceId));
}

TEST(DeviceIdTest, HashNeverContainsTheRawId)
{
    const auto hash = posture::hashDeviceId(kDeviceId);
    EXPECT_EQ(hash.size(), 64u);
    EXPECT_EQ(hash.find("550e8400"), std::string::npos);
}
