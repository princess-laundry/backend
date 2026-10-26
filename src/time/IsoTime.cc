#include "time/IsoTime.h"

#include <cstdio>

namespace posture
{
namespace
{

constexpr std::int64_t kMicrosPerSecond = 1000000;
constexpr std::int64_t kMicrosPerDay = 86400 * kMicrosPerSecond;

// Reads exactly `count` decimal digits starting at `pos`.
bool readDigits(const std::string &text,
                std::size_t pos,
                std::size_t count,
                int &out)
{
    if (pos + count > text.size())
        return false;

    int value = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const char c = text[pos + i];
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

bool isLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month)
{
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && isLeapYear(year) ? 29 : kDays[month - 1];
}

// Reads YYYY-MM-DD at the start of `text`.
std::optional<LocalDate> readDate(const std::string &text)
{
    LocalDate date;
    if (!readDigits(text, 0, 4, date.year) || text.size() < 10 ||
        text[4] != '-' || !readDigits(text, 5, 2, date.month) ||
        text[7] != '-' || !readDigits(text, 8, 2, date.day))
    {
        return std::nullopt;
    }
    if (date.year < 1 || date.month < 1 || date.month > 12 || date.day < 1 ||
        date.day > daysInMonth(date.year, date.month))
    {
        return std::nullopt;
    }
    return date;
}

std::int64_t floorDiv(std::int64_t a, std::int64_t b)
{
    auto q = a / b;
    if (a % b != 0 && ((a < 0) != (b < 0)))
        --q;
    return q;
}

}  // namespace

bool operator==(const LocalDate &a, const LocalDate &b)
{
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

// Howard Hinnant's days_from_civil and civil_from_days.
std::int64_t daysFromCivil(const LocalDate &date)
{
    const int y = date.year - (date.month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const auto mp = static_cast<unsigned>((date.month + 9) % 12);
    const auto doy = (153 * mp + 2) / 5 + static_cast<unsigned>(date.day) - 1;
    const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + doe - 719468;
}

LocalDate civilFromDays(std::int64_t days)
{
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto doe = static_cast<unsigned>(days - era * 146097);
    const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const auto mp = (5 * doy + 2) / 153;
    const auto day = doy - (153 * mp + 2) / 5 + 1;
    const auto month = mp < 10 ? mp + 3 : mp - 9;
    const auto year = static_cast<std::int64_t>(yoe) + era * 400 +
                      (month <= 2 ? 1 : 0);
    return {static_cast<int>(year),
            static_cast<int>(month),
            static_cast<int>(day)};
}

std::optional<LocalDate> parseLocalDate(const std::string &text)
{
    if (text.size() != 10)
        return std::nullopt;
    return readDate(text);
}

std::string formatLocalDate(const LocalDate &date)
{
    char buf[16];
    std::snprintf(
        buf, sizeof buf, "%04d-%02d-%02d", date.year, date.month, date.day);
    return buf;
}

std::optional<std::int64_t> parseTimestampMicros(const std::string &text)
{
    const auto date = readDate(text);
    if (!date)
        return std::nullopt;

    int hour = 0;
    int minute = 0;
    int second = 0;
    if (text.size() < 16 || text[10] != 'T' ||
        !readDigits(text, 11, 2, hour) || text[13] != ':' ||
        !readDigits(text, 14, 2, minute))
    {
        return std::nullopt;
    }
    std::size_t pos = 16;

    std::int64_t fractionMicros = 0;
    if (pos < text.size() && text[pos] == ':')
    {
        if (!readDigits(text, pos + 1, 2, second))
            return std::nullopt;
        pos += 3;

        if (pos < text.size() && text[pos] == '.')
        {
            ++pos;
            std::size_t digits = 0;
            std::int64_t scale = 100000;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
            {
                if (++digits > 9)
                    return std::nullopt;
                fractionMicros += (text[pos] - '0') * scale;
                scale /= 10;
                ++pos;
            }
            if (digits == 0)
                return std::nullopt;
        }
    }
    if (hour > 23 || minute > 59 || second > 59)
        return std::nullopt;

    if (pos >= text.size())
        return std::nullopt;

    std::int64_t offsetSec = 0;
    if (text[pos] == 'Z')
    {
        ++pos;
    }
    else if (text[pos] == '+' || text[pos] == '-')
    {
        int offsetHour = 0;
        int offsetMinute = 0;
        if (!readDigits(text, pos + 1, 2, offsetHour) ||
            pos + 3 >= text.size() || text[pos + 3] != ':' ||
            !readDigits(text, pos + 4, 2, offsetMinute))
        {
            return std::nullopt;
        }
        // java.time.ZoneOffset allows at most 18 hours either way.
        if (offsetMinute > 59 || offsetHour * 60 + offsetMinute > 18 * 60)
            return std::nullopt;
        offsetSec = (offsetHour * 60 + offsetMinute) * 60;
        if (text[pos] == '-')
            offsetSec = -offsetSec;
        pos += 6;
    }
    else
    {
        return std::nullopt;
    }
    if (pos != text.size())
        return std::nullopt;

    const std::int64_t localSec =
        daysFromCivil(*date) * 86400 + hour * 3600 + minute * 60 + second;
    return (localSec - offsetSec) * kMicrosPerSecond + fractionMicros;
}

std::string formatSeoulTimestamp(std::int64_t epochMicros)
{
    const auto local = epochMicros + kSeoulOffsetSec * kMicrosPerSecond;
    const auto days = floorDiv(local, kMicrosPerDay);
    const auto timeOfDay = local - days * kMicrosPerDay;
    const auto date = civilFromDays(days);

    const auto secondOfDay = timeOfDay / kMicrosPerSecond;
    const auto fraction = timeOfDay % kMicrosPerSecond;

    char buf[48];
    int len = std::snprintf(buf,
                            sizeof buf,
                            "%04d-%02d-%02dT%02d:%02d:%02d",
                            date.year,
                            date.month,
                            date.day,
                            static_cast<int>(secondOfDay / 3600),
                            static_cast<int>(secondOfDay / 60 % 60),
                            static_cast<int>(secondOfDay % 60));
    if (fraction % 1000 == 0 && fraction != 0)
    {
        len += std::snprintf(buf + len,
                             sizeof buf - len,
                             ".%03d",
                             static_cast<int>(fraction / 1000));
    }
    else if (fraction != 0)
    {
        len += std::snprintf(
            buf + len, sizeof buf - len, ".%06d", static_cast<int>(fraction));
    }
    std::snprintf(buf + len, sizeof buf - len, "+09:00");
    return buf;
}

}  // namespace posture
