#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "time/IsoTime.h"

namespace posture
{

// Share of classified time spent in the NORMAL posture:
// normal / (normal + fhp + lean). AWAY time is not part of either side.
// Returns nullopt when no posture time was classified, so callers can tell
// "no data" apart from "never normal".
std::optional<double> normalRatio(std::int64_t normalSec,
                                  std::int64_t fhpSec,
                                  std::int64_t leanSec);

// Longest range GET /api/v1/stats/daily serves, counting both ends.
constexpr std::int64_t kMaxStatsRangeDays = 366;

// An inclusive range of Seoul calendar days.
struct DateRange
{
    LocalDate from;
    LocalDate to;
};

// Parses the from and to query parameters. Both must be YYYY-MM-DD dates,
// from must not be after to, and the range may cover at most
// kMaxStatsRangeDays days. Throws std::invalid_argument otherwise.
DateRange parseDateRange(const std::string &from, const std::string &to);

}  // namespace posture
