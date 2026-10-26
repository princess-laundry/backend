#include "time/IsoTime.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace
{

using posture::formatSeoulTimestamp;
using posture::LocalDate;
using posture::parseLocalDate;
using posture::parseTimestampMicros;

constexpr std::int64_t kSecond = 1000000;
// date -u -d 2026-10-16T12:30:00Z +%s
constexpr std::int64_t kNoonUtc = 1792153800 * kSecond;

}  // namespace

TEST(IsoTimeTest, ParsesEveryOffsetToTheSameInstant)
{
    EXPECT_EQ(parseTimestampMicros("2026-10-16T12:30:00Z"), kNoonUtc);
    EXPECT_EQ(parseTimestampMicros("2026-10-16T21:30:00+09:00"), kNoonUtc);
    EXPECT_EQ(parseTimestampMicros("2026-10-16T08:30:00-04:00"), kNoonUtc);
    EXPECT_EQ(parseTimestampMicros("2026-10-16T18:00:00+05:30"), kNoonUtc);
    EXPECT_EQ(parseTimestampMicros("1970-01-01T00:00:00Z"), 0);
}

TEST(IsoTimeTest, SecondsAreOptional)
{
    EXPECT_EQ(parseTimestampMicros("2026-10-16T21:30+09:00"), kNoonUtc);
}

TEST(IsoTimeTest, ReadsFractionsUpToMicroseconds)
{
    EXPECT_EQ(parseTimestampMicros("2026-10-16T21:30:00.5+09:00"),
              kNoonUtc + 500000);
    EXPECT_EQ(parseTimestampMicros("2026-10-16T21:30:00.123+09:00"),
              kNoonUtc + 123000);
    EXPECT_EQ(parseTimestampMicros("2026-10-16T21:30:00.123456+09:00"),
              kNoonUtc + 123456);
    // Nanoseconds from java.time are truncated, not rounded.
    EXPECT_EQ(parseTimestampMicros("2026-10-16T21:30:00.123456789+09:00"),
              kNoonUtc + 123456);
}

TEST(IsoTimeTest, AcceptsLeapDays)
{
    EXPECT_TRUE(parseTimestampMicros("2024-02-29T10:00:00+09:00"));
    EXPECT_TRUE(parseTimestampMicros("2000-02-29T10:00:00+09:00"));
}

TEST(IsoTimeTest, RejectsMalformedTimestamps)
{
    for (const char *text : {"",
                             "2026-10-16",
                             "2026-10-16T21:30:00",
                             "2026-10-16 21:30:00+09:00",
                             "2026-10-16t21:30:00+09:00",
                             "2026-10-16T21:30:00z",
                             "2026-10-16T21:30:00+0900",
                             "2026-10-16T21:30:00+09",
                             "2026-10-16T21:30:00+19:00",
                             "2026-10-16T21:30:00+09:60",
                             "2026-10-16T21:30:00.+09:00",
                             "2026-10-16T21:30:00.1234567890+09:00",
                             "2026-10-16T21:30:00+09:00 ",
                             "2026-10-16T21:30:00+09:00Z",
                             "2026-10-16T21:3:00+09:00",
                             "2026-10-16T24:00:00+09:00",
                             "2026-10-16T21:60:00+09:00",
                             "2026-10-16T21:30:60+09:00",
                             "2026-13-16T21:30:00+09:00",
                             "2026-00-16T21:30:00+09:00",
                             "2026-02-29T21:30:00+09:00",
                             "1900-02-29T21:30:00+09:00",
                             "2026-04-31T21:30:00+09:00",
                             "2026-10-00T21:30:00+09:00",
                             "+2026-10-16T21:30:00+09:00"})
    {
        EXPECT_FALSE(parseTimestampMicros(text)) << "text: '" << text << "'";
    }
}

TEST(IsoTimeTest, FormatsInSeoulTime)
{
    EXPECT_EQ(formatSeoulTimestamp(kNoonUtc), "2026-10-16T21:30:00+09:00");
    EXPECT_EQ(formatSeoulTimestamp(0), "1970-01-01T09:00:00+09:00");
}

TEST(IsoTimeTest, FormatRollsOverSeoulMidnight)
{
    const auto lateUtc = parseTimestampMicros("2026-10-16T15:30:00Z");
    ASSERT_TRUE(lateUtc);
    EXPECT_EQ(formatSeoulTimestamp(*lateUtc), "2026-10-17T00:30:00+09:00");
}

TEST(IsoTimeTest, FormatPrintsOnlyTheFractionThatExists)
{
    EXPECT_EQ(formatSeoulTimestamp(kNoonUtc + 120000),
              "2026-10-16T21:30:00.120+09:00");
    EXPECT_EQ(formatSeoulTimestamp(kNoonUtc + 123456),
              "2026-10-16T21:30:00.123456+09:00");
    EXPECT_EQ(formatSeoulTimestamp(kNoonUtc + 1),
              "2026-10-16T21:30:00.000001+09:00");
}

TEST(IsoTimeTest, FormatHandlesInstantsBeforeTheEpoch)
{
    EXPECT_EQ(formatSeoulTimestamp(-500000), "1970-01-01T08:59:59.500+09:00");
    EXPECT_EQ(formatSeoulTimestamp(-10 * 3600 * kSecond),
              "1969-12-31T23:00:00+09:00");
}

TEST(IsoTimeTest, FormatThenParseRoundTrips)
{
    for (const std::int64_t micros :
         {kNoonUtc, kNoonUtc + 1, kNoonUtc + 999999, std::int64_t{0}})
    {
        EXPECT_EQ(parseTimestampMicros(formatSeoulTimestamp(micros)), micros);
    }
}

TEST(IsoTimeTest, ParsesLocalDates)
{
    const auto date = parseLocalDate("2026-10-16");
    ASSERT_TRUE(date);
    EXPECT_EQ(date->year, 2026);
    EXPECT_EQ(date->month, 10);
    EXPECT_EQ(date->day, 16);
    EXPECT_TRUE(parseLocalDate("2024-02-29"));
    EXPECT_TRUE(parseLocalDate("0001-01-01"));
    EXPECT_TRUE(parseLocalDate("9999-12-31"));
}

TEST(IsoTimeTest, RejectsMalformedLocalDates)
{
    for (const char *text : {"",
                             "2026-1-16",
                             "2026-10-6",
                             "20261016",
                             "2026/10/16",
                             "2026-10-16T00:00:00",
                             "2026-10-16 ",
                             "0000-01-01",
                             "2026-02-29",
                             "2026-13-01",
                             "2026-12-32",
                             "abcd-ef-gh"})
    {
        EXPECT_FALSE(parseLocalDate(text)) << "text: '" << text << "'";
    }
}

TEST(IsoTimeTest, ConvertsBetweenDatesAndDayNumbers)
{
    EXPECT_EQ(posture::daysFromCivil({1970, 1, 1}), 0);
    EXPECT_EQ(posture::daysFromCivil({2000, 3, 1}), 11017);
    EXPECT_EQ(posture::daysFromCivil({2026, 10, 16}), 20742);
    EXPECT_EQ(posture::daysFromCivil({1969, 12, 31}), -1);

    // Every day from 1900 to 2100 survives the round trip and the format.
    for (std::int64_t day = posture::daysFromCivil({1900, 1, 1});
         day <= posture::daysFromCivil({2100, 12, 31});
         ++day)
    {
        const auto date = posture::civilFromDays(day);
        ASSERT_EQ(posture::daysFromCivil(date), day);
        const auto parsed = parseLocalDate(posture::formatLocalDate(date));
        ASSERT_TRUE(parsed);
        ASSERT_TRUE(*parsed == date);
    }
}
