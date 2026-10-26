#include "stats/DailyStats.h"

#include <stdexcept>

namespace posture
{

std::optional<double> normalRatio(std::int64_t normalSec,
                                  std::int64_t fhpSec,
                                  std::int64_t leanSec)
{
    const auto total = normalSec + fhpSec + leanSec;
    if (total <= 0)
        return std::nullopt;
    return static_cast<double>(normalSec) / static_cast<double>(total);
}

DateRange parseDateRange(const std::string &from, const std::string &to)
{
    const auto fromDate = parseLocalDate(from);
    if (!fromDate)
        throw std::invalid_argument("from must be a date in YYYY-MM-DD form");
    const auto toDate = parseLocalDate(to);
    if (!toDate)
        throw std::invalid_argument("to must be a date in YYYY-MM-DD form");

    const auto days = daysFromCivil(*toDate) - daysFromCivil(*fromDate) + 1;
    if (days < 1)
        throw std::invalid_argument("from must not be after to");
    if (days > kMaxStatsRangeDays)
    {
        throw std::invalid_argument("the range may cover at most " +
                                    std::to_string(kMaxStatsRangeDays) +
                                    " days");
    }
    return {*fromDate, *toDate};
}

}  // namespace posture
