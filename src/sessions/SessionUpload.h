#pragma once

#include <json/value.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace posture
{

// Mirrors the posture_state enum in the database.
enum class PostureState
{
    Normal,
    Fhp,
    Lean,
    Away
};

// The database spelling: NORMAL, FHP, LEAN or AWAY.
const char *postureStateName(PostureState state);
// Case sensitive inverse of postureStateName.
std::optional<PostureState> parsePostureState(const std::string &name);

struct SessionSummary
{
    int studySec = 0;
    int awaySec = 0;
    int normalSec = 0;
    int fhpSec = 0;
    int leanSec = 0;
    int alertCount = 0;
};

// A stretch of one posture state, in milliseconds from the session start.
struct PostureSegment
{
    PostureState state = PostureState::Normal;
    int startMs = 0;
    int endMs = 0;
    std::optional<double> avgRatio;
    std::optional<double> avgAngle;
};

// A validated POST /api/v1/sessions body.
struct SessionUpload
{
    // Lowercase canonical UUID.
    std::string clientSessionId;
    // Microseconds since the Unix epoch.
    std::int64_t startedAtUs = 0;
    std::int64_t endedAtUs = 0;
    int thresholdVersion = 0;
    std::string appVersion;
    SessionSummary summary;
    std::vector<PostureSegment> segments;
};

// Upper bound on one session. It keeps segment offsets well inside the INT
// columns and rejects clocks that jumped while a session was recorded.
constexpr std::int64_t kMaxSessionSec = 24 * 60 * 60;
constexpr std::size_t kMaxAppVersionLength = 64;

// Validates an upload body and returns it in typed form. Throws
// std::invalid_argument with a message that names the offending field.
//
// client_session_id is a UUID string. started_at and ended_at are ISO 8601
// timestamps with an offset, on or after 2000-01-01, with ended_at >=
// started_at and at most kMaxSessionSec apart. threshold_version is a
// positive integer; whether that version exists is checked against the
// database by the caller. app_version is 1 to 64 printable ASCII characters.
//
// Every summary value is a non-negative 32 bit integer. JSON numbers written
// with a fraction or exponent are rejected even when they are whole.
// study_sec + away_sec and normal_sec + fhp_sec + lean_sec may each exceed
// the session length by at most one second per term, which allows for every
// value being rounded on its own.
//
// segments is an array, possibly empty, ordered by time and not overlapping.
// Each segment has a known state, 0 <= start_ms <= end_ms, ends no later than
// the session does, and optional finite avg_ratio and avg_angle values that
// fit a REAL column.
//
// Unknown members are ignored so newer apps can send extra fields.
SessionUpload parseSessionUpload(const Json::Value &body);

// Segment columns as PostgreSQL array literals, so any number of segments
// can be inserted with one statement through unnest().
struct SegmentArrays
{
    std::string states;
    std::string startMs;
    std::string endMs;
    std::string avgRatios;
    std::string avgAngles;
};

SegmentArrays toSegmentArrays(const std::vector<PostureSegment> &segments);

// Parses the {id} path segment: a positive decimal integer without sign or
// leading zeros.
std::optional<std::int64_t> parseSessionId(const std::string &text);

}  // namespace posture
