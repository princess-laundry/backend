#include "stats/DailyStats.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace
{

using posture::normalRatio;
using posture::parseDateRange;

std::string rangeError(const std::string &from, const std::string &to)
{
    try
    {
        parseDateRange(from, to);
        return "";
    }
    catch (const std::invalid_argument &e)
    {
        return e.what();
    }
}

}  // namespace

TEST(DailyStatsTest, NormalRatioIsNormalOverClassifiedTime)
{
    EXPECT_EQ(normalRatio(300, 60, 40), 0.75);
    EXPECT_EQ(normalRatio(100, 0, 0), 1.0);
    EXPECT_EQ(normalRatio(0, 30, 10), 0.0);
    EXPECT_DOUBLE_EQ(*normalRatio(2, 1, 0), 2.0 / 3.0);
}

TEST(DailyStatsTest, NormalRatioIsNullWithoutClassifiedTime)
{
    EXPECT_FALSE(normalRatio(0, 0, 0));
}

TEST(DailyStatsTest, NormalRatioHandlesLargeTotals)
{
    // A year of daily rows summed into 64 bit counters.
    const std::int64_t big = 366LL * 86400;
    EXPECT_EQ(normalRatio(big, big, 0), 0.5);
}

TEST(DailyStatsTest, AcceptsValidRanges)
{
    const auto single = parseDateRange("2026-10-16", "2026-10-16");
    EXPECT_TRUE(single.from == single.to);

    const auto year = parseDateRange("2026-01-01", "2026-12-31");
    EXPECT_EQ(year.from.year, 2026);
    EXPECT_EQ(year.to.month, 12);
    EXPECT_EQ(year.to.day, 31);

    // 366 days including both ends, in a common year and across a leap day.
    EXPECT_EQ(rangeError("2026-01-01", "2027-01-01"), "");
    EXPECT_EQ(rangeError("2024-01-01", "2024-12-31"), "");
}

TEST(DailyStatsTest, RejectsRangesLongerThan366Days)
{
    const std::string expected = "the range may cover at most 366 days";
    EXPECT_EQ(rangeError("2026-01-01", "2027-01-02"), expected);
    EXPECT_EQ(rangeError("2024-01-01", "2025-01-01"), expected);
    EXPECT_EQ(rangeError("2000-01-01", "2026-10-16"), expected);
}

TEST(DailyStatsTest, RejectsInvertedRanges)
{
    EXPECT_EQ(rangeError("2026-10-17", "2026-10-16"),
              "from must not be after to");
    EXPECT_EQ(rangeError("2027-01-01", "2026-12-31"),
              "from must not be after to");
}

TEST(DailyStatsTest, RejectsMalformedDates)
{
    EXPECT_EQ(rangeError("", "2026-10-16"),
              "from must be a date in YYYY-MM-DD form");
    EXPECT_EQ(rangeError("2026-10-16", ""),
              "to must be a date in YYYY-MM-DD form");
    EXPECT_EQ(rangeError("2026-02-30", "2026-03-01"),
              "from must be a date in YYYY-MM-DD form");
    EXPECT_EQ(rangeError("2026-10-01", "2026-10-1"),
              "to must be a date in YYYY-MM-DD form");
}
