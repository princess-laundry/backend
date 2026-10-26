#include "sessions/SessionUpload.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "auth/DeviceId.h"
#include "time/IsoTime.h"

namespace posture
{
namespace
{

constexpr std::int64_t kMicrosPerSecond = 1000000;
constexpr std::int64_t kMaxInt = std::numeric_limits<int>::max();
// 2000-01-01T00:00:00Z. Nothing recorded by the app can be older, and it
// keeps every stored instant inside the fixed +09:00 era of Asia/Seoul.
constexpr std::int64_t kEarliestStartUs = 946684800 * kMicrosPerSecond;

[[noreturn]] void reject(const std::string &message)
{
    throw std::invalid_argument(message);
}

// A member of a JSON object together with the name used in error messages,
// such as "summary.study_sec". Missing members read as null.
struct Field
{
    const Json::Value &value;
    std::string name;
};

Field field(const Json::Value &object,
            const std::string &key,
            const std::string &prefix = "")
{
    static const Json::Value kNull;
    const auto *found = object.find(key.data(), key.data() + key.size());
    return {found ? *found : kNull, prefix + key};
}

int requireInt(const Field &f, std::int64_t min, std::int64_t max)
{
    const auto type = f.value.type();
    if (type != Json::intValue && type != Json::uintValue)
        reject(f.name + " must be an integer");
    if (!f.value.isInt64() || f.value.asInt64() < min ||
        f.value.asInt64() > max)
    {
        reject(f.name + " must be between " + std::to_string(min) + " and " +
               std::to_string(max));
    }
    return static_cast<int>(f.value.asInt64());
}

int requireCount(const Field &f)
{
    return requireInt(f, 0, kMaxInt);
}

std::int64_t requireTimestamp(const Field &f)
{
    std::optional<std::int64_t> micros;
    if (f.value.isString())
        micros = parseTimestampMicros(f.value.asString());
    if (!micros)
    {
        reject(f.name +
               " must be an ISO 8601 timestamp with a UTC offset, such as "
               "2026-10-16T21:30:00+09:00");
    }
    return *micros;
}

std::optional<double> optionalReal(const Field &f)
{
    if (f.value.isNull())
        return std::nullopt;
    if (!f.value.isNumeric())
        reject(f.name + " must be a number or null");

    const double real = f.value.asDouble();
    if (!std::isfinite(real) ||
        std::fabs(real) > std::numeric_limits<float>::max())
    {
        reject(f.name + " is out of range");
    }
    return real;
}

std::string requireAppVersion(const Field &f)
{
    if (!f.value.isString())
        reject(f.name + " must be a string");

    const auto version = f.value.asString();
    const bool printable =
        std::all_of(version.begin(), version.end(), [](char c) {
            return c >= 0x20 && c <= 0x7e;
        });
    if (version.empty() || version.size() > kMaxAppVersionLength || !printable)
    {
        reject(f.name + " must be 1 to " +
               std::to_string(kMaxAppVersionLength) +
               " printable ASCII characters");
    }
    return version;
}

SessionSummary requireSummary(const Field &f, std::int64_t durationUs)
{
    if (!f.value.isObject())
        reject(f.name + " must be an object");

    const std::string prefix = f.name + ".";
    SessionSummary summary;
    summary.studySec = requireCount(field(f.value, "study_sec", prefix));
    summary.awaySec = requireCount(field(f.value, "away_sec", prefix));
    summary.normalSec = requireCount(field(f.value, "normal_sec", prefix));
    summary.fhpSec = requireCount(field(f.value, "fhp_sec", prefix));
    summary.leanSec = requireCount(field(f.value, "lean_sec", prefix));
    summary.alertCount = requireCount(field(f.value, "alert_count", prefix));

    const auto fits = [durationUs](std::int64_t totalSec, int terms) {
        return totalSec * kMicrosPerSecond <=
               durationUs + terms * kMicrosPerSecond;
    };
    if (!fits(std::int64_t{summary.studySec} + summary.awaySec, 2))
    {
        reject("summary.study_sec + summary.away_sec exceeds the session "
               "length");
    }
    const std::int64_t postureSec =
        std::int64_t{summary.normalSec} + summary.fhpSec + summary.leanSec;
    if (!fits(postureSec, 3))
    {
        reject("summary.normal_sec + summary.fhp_sec + summary.lean_sec "
               "exceeds the session length");
    }
    return summary;
}

PostureSegment requireSegment(const Field &f)
{
    if (!f.value.isObject())
        reject(f.name + " must be an object");

    const std::string prefix = f.name + ".";
    PostureSegment segment;

    const auto state = field(f.value, "state", prefix);
    std::optional<PostureState> parsed;
    if (state.value.isString())
        parsed = parsePostureState(state.value.asString());
    if (!parsed)
        reject(state.name + " must be one of NORMAL, FHP, LEAN, AWAY");
    segment.state = *parsed;

    segment.startMs = requireCount(field(f.value, "start_ms", prefix));
    segment.endMs = requireCount(field(f.value, "end_ms", prefix));
    if (segment.endMs < segment.startMs)
        reject(f.name + ".end_ms must not be less than start_ms");

    segment.avgRatio = optionalReal(field(f.value, "avg_ratio", prefix));
    segment.avgAngle = optionalReal(field(f.value, "avg_angle", prefix));
    return segment;
}

std::vector<PostureSegment> requireSegments(const Field &f,
                                            std::int64_t durationUs)
{
    if (!f.value.isArray())
        reject(f.name + " must be an array");

    // Segment offsets are whole milliseconds, so a segment may end in the
    // millisecond the session ends in.
    const auto durationMs = (durationUs + 999) / 1000;

    std::vector<PostureSegment> segments;
    segments.reserve(f.value.size());
    for (Json::ArrayIndex i = 0; i < f.value.size(); ++i)
    {
        const auto name = f.name + "[" + std::to_string(i) + "]";
        auto segment = requireSegment({f.value[i], name});
        if (!segments.empty() && segment.startMs < segments.back().endMs)
        {
            reject(name + " starts before " + f.name + "[" +
                   std::to_string(i - 1) + "] ends");
        }
        if (segment.endMs > durationMs)
            reject(name + ".end_ms is past the end of the session");
        segments.push_back(segment);
    }
    return segments;
}

void appendReal(std::string &out, const std::optional<double> &value)
{
    if (!value)
    {
        out += "NULL";
        return;
    }
    // Shortest text that reads back as the same double, independent of the
    // process locale.
    char buf[32];
    const auto result = std::to_chars(buf, buf + sizeof buf, *value);
    out.append(buf, result.ptr);
}

template <typename Append>
std::string arrayLiteral(const std::vector<PostureSegment> &segments,
                         Append append)
{
    std::string out = "{";
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        if (i > 0)
            out += ',';
        append(out, segments[i]);
    }
    out += '}';
    return out;
}

}  // namespace

const char *postureStateName(PostureState state)
{
    switch (state)
    {
        case PostureState::Normal:
            return "NORMAL";
        case PostureState::Fhp:
            return "FHP";
        case PostureState::Lean:
            return "LEAN";
        case PostureState::Away:
            return "AWAY";
    }
    return "NORMAL";
}

std::optional<PostureState> parsePostureState(const std::string &name)
{
    for (const auto state : {PostureState::Normal,
                             PostureState::Fhp,
                             PostureState::Lean,
                             PostureState::Away})
    {
        if (name == postureStateName(state))
            return state;
    }
    return std::nullopt;
}

SessionUpload parseSessionUpload(const Json::Value &body)
{
    if (!body.isObject())
        reject("body must be a JSON object");

    SessionUpload upload;

    const auto clientSessionId = field(body, "client_session_id");
    if (!clientSessionId.value.isString() ||
        !isUuid(clientSessionId.value.asString()))
    {
        reject(clientSessionId.name + " must be a UUID string");
    }
    upload.clientSessionId = clientSessionId.value.asString();
    std::transform(upload.clientSessionId.begin(),
                   upload.clientSessionId.end(),
                   upload.clientSessionId.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });

    upload.startedAtUs = requireTimestamp(field(body, "started_at"));
    upload.endedAtUs = requireTimestamp(field(body, "ended_at"));
    if (upload.startedAtUs < kEarliestStartUs)
        reject("started_at must not be before 2000-01-01");
    if (upload.endedAtUs < upload.startedAtUs)
        reject("ended_at must not be before started_at");
    const auto durationUs = upload.endedAtUs - upload.startedAtUs;
    if (durationUs > kMaxSessionSec * kMicrosPerSecond)
    {
        reject("a session may last at most " + std::to_string(kMaxSessionSec) +
               " seconds");
    }

    upload.thresholdVersion =
        requireInt(field(body, "threshold_version"), 1, kMaxInt);
    upload.appVersion = requireAppVersion(field(body, "app_version"));
    upload.summary = requireSummary(field(body, "summary"), durationUs);
    upload.segments = requireSegments(field(body, "segments"), durationUs);
    return upload;
}

SegmentArrays toSegmentArrays(const std::vector<PostureSegment> &segments)
{
    using Out = std::string;
    using Segment = PostureSegment;

    SegmentArrays arrays;
    arrays.states = arrayLiteral(segments, [](Out &out, const Segment &s) {
        out += postureStateName(s.state);
    });
    arrays.startMs = arrayLiteral(segments, [](Out &out, const Segment &s) {
        out += std::to_string(s.startMs);
    });
    arrays.endMs = arrayLiteral(segments, [](Out &out, const Segment &s) {
        out += std::to_string(s.endMs);
    });
    arrays.avgRatios = arrayLiteral(segments, [](Out &out, const Segment &s) {
        appendReal(out, s.avgRatio);
    });
    arrays.avgAngles = arrayLiteral(segments, [](Out &out, const Segment &s) {
        appendReal(out, s.avgAngle);
    });
    return arrays;
}

std::optional<std::int64_t> parseSessionId(const std::string &text)
{
    // BIGSERIAL values stay far below 10^18, so 18 digits cannot overflow.
    if (text.empty() || text.size() > 18 || text[0] == '0')
        return std::nullopt;

    std::int64_t value = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
            return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

}  // namespace posture
