#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace posture
{

// Korea has used a fixed UTC+09:00 offset without daylight saving since
// 1988, so Seoul local time is UTC shifted by nine hours.
constexpr std::int64_t kSeoulOffsetSec = 9 * 60 * 60;

// A calendar date in the proleptic Gregorian calendar.
struct LocalDate
{
    int year = 1970;
    int month = 1;
    int day = 1;
};

bool operator==(const LocalDate &a, const LocalDate &b);

// Days since 1970-01-01. Negative for earlier dates.
std::int64_t daysFromCivil(const LocalDate &date);
LocalDate civilFromDays(std::int64_t days);

// Accepts exactly YYYY-MM-DD with a real calendar day and a year from 1 to
// 9999.
std::optional<LocalDate> parseLocalDate(const std::string &text);
std::string formatLocalDate(const LocalDate &date);

// Parses an ISO 8601 timestamp that carries its UTC offset and returns
// microseconds since the Unix epoch. The accepted form is
// YYYY-MM-DDTHH:MM[:SS[.fraction]] followed by Z or +HH:MM / -HH:MM. Seconds
// are optional because java.time prints them only when they are not zero.
// Up to nine fraction digits are read; anything past microseconds is
// truncated.
std::optional<std::int64_t> parseTimestampMicros(const std::string &text);

// Formats an instant as Seoul local time with a +09:00 offset. The fraction
// is omitted for whole seconds and printed with 3 or 6 digits otherwise.
std::string formatSeoulTimestamp(std::int64_t epochMicros);

}  // namespace posture
