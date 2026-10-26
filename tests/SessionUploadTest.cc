#include "sessions/SessionUpload.h"

#include <gtest/gtest.h>
#include <json/reader.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "time/IsoTime.h"

namespace
{

using posture::parseSessionUpload;
using posture::PostureState;

Json::Value parseJson(const std::string &text)
{
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value value;
    std::string errors;
    if (!reader->parse(text.data(), text.data() + text.size(), &value, &errors))
        throw std::runtime_error("bad test JSON: " + errors);
    return value;
}

// A one hour session: 50 minutes at the desk, 10 minutes away.
Json::Value validBody()
{
    return parseJson(R"json({
        "client_session_id": "3F2504E0-4F89-11D3-9A0C-0305E82C3301",
        "started_at": "2026-10-16T21:00:00+09:00",
        "ended_at": "2026-10-16T22:00:00+09:00",
        "threshold_version": 1,
        "app_version": "1.0.0 (12)",
        "summary": {
            "study_sec": 3000,
            "away_sec": 600,
            "normal_sec": 2000,
            "fhp_sec": 700,
            "lean_sec": 300,
            "alert_count": 4
        },
        "segments": [
            {"state": "NORMAL", "start_ms": 0, "end_ms": 2000000,
             "avg_ratio": 0.12, "avg_angle": 8.5},
            {"state": "FHP", "start_ms": 2000000, "end_ms": 2700000,
             "avg_ratio": 0.31, "avg_angle": null},
            {"state": "LEAN", "start_ms": 2700000, "end_ms": 3000000},
            {"state": "AWAY", "start_ms": 3000000, "end_ms": 3600000}
        ],
        "extra_field_from_a_newer_app": true
    })json");
}

// The validation message for a body, or "" when it is accepted.
std::string errorFor(const Json::Value &body)
{
    try
    {
        parseSessionUpload(body);
        return "";
    }
    catch (const std::invalid_argument &e)
    {
        return e.what();
    }
}

std::string errorAfter(const std::function<void(Json::Value &)> &change)
{
    auto body = validBody();
    change(body);
    return errorFor(body);
}

}  // namespace

TEST(SessionUploadTest, AcceptsAValidBody)
{
    const auto upload = parseSessionUpload(validBody());

    EXPECT_EQ(upload.clientSessionId, "3f2504e0-4f89-11d3-9a0c-0305e82c3301");
    EXPECT_EQ(upload.startedAtUs,
              posture::parseTimestampMicros("2026-10-16T12:00:00Z"));
    EXPECT_EQ(upload.endedAtUs - upload.startedAtUs, 3600LL * 1000000);
    EXPECT_EQ(upload.thresholdVersion, 1);
    EXPECT_EQ(upload.appVersion, "1.0.0 (12)");

    EXPECT_EQ(upload.summary.studySec, 3000);
    EXPECT_EQ(upload.summary.awaySec, 600);
    EXPECT_EQ(upload.summary.normalSec, 2000);
    EXPECT_EQ(upload.summary.fhpSec, 700);
    EXPECT_EQ(upload.summary.leanSec, 300);
    EXPECT_EQ(upload.summary.alertCount, 4);

    ASSERT_EQ(upload.segments.size(), 4u);
    EXPECT_EQ(upload.segments[0].state, PostureState::Normal);
    EXPECT_EQ(upload.segments[0].avgRatio, 0.12);
    EXPECT_EQ(upload.segments[0].avgAngle, 8.5);
    EXPECT_EQ(upload.segments[1].state, PostureState::Fhp);
    EXPECT_FALSE(upload.segments[1].avgAngle);
    EXPECT_EQ(upload.segments[2].state, PostureState::Lean);
    EXPECT_FALSE(upload.segments[2].avgRatio);
    EXPECT_EQ(upload.segments[3].state, PostureState::Away);
    EXPECT_EQ(upload.segments[3].startMs, 3000000);
    EXPECT_EQ(upload.segments[3].endMs, 3600000);
}

TEST(SessionUploadTest, AcceptsEdgeCases)
{
    // No segments at all.
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["segments"] = Json::Value(Json::arrayValue);
              }),
              "");
    // A zero length session with zero length segments.
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["ended_at"] = b["started_at"];
                  b["summary"] = parseJson(
                      R"({"study_sec":0,"away_sec":0,"normal_sec":0,
                          "fhp_sec":0,"lean_sec":0,"alert_count":0})");
                  b["segments"] = parseJson(
                      R"([{"state":"NORMAL","start_ms":0,"end_ms":0}])");
              }),
              "");
    // Rounded values a second over the session length on each term.
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["summary"]["study_sec"] = 3001;
                  b["summary"]["away_sec"] = 601;
              }),
              "");
    // A segment that ends inside the last partial millisecond.
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["ended_at"] = "2026-10-16T22:00:00.0005+09:00";
                  b["segments"][3]["end_ms"] = 3600001;
              }),
              "");
    // Exactly the longest allowed session, written with another offset.
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["started_at"] = "2026-10-16T12:00:00Z";
                  b["ended_at"] = "2026-10-17T12:00:00Z";
              }),
              "");
}

TEST(SessionUploadTest, RejectsBodiesThatAreNotObjects)
{
    EXPECT_EQ(errorFor(Json::Value()), "body must be a JSON object");
    EXPECT_EQ(errorFor(parseJson("[]")), "body must be a JSON object");
    EXPECT_EQ(errorFor(parseJson("\"text\"")), "body must be a JSON object");
}

TEST(SessionUploadTest, RejectsMissingMembers)
{
    for (const char *key : {"client_session_id",
                            "started_at",
                            "ended_at",
                            "threshold_version",
                            "app_version",
                            "summary",
                            "segments"})
    {
        const auto error =
            errorAfter([key](Json::Value &b) { b.removeMember(key); });
        EXPECT_EQ(error.rfind(key, 0), 0u) << key << ": " << error;
    }
    for (const char *key : {"study_sec",
                            "away_sec",
                            "normal_sec",
                            "fhp_sec",
                            "lean_sec",
                            "alert_count"})
    {
        const auto error = errorAfter(
            [key](Json::Value &b) { b["summary"].removeMember(key); });
        EXPECT_EQ(error, std::string("summary.") + key + " must be an integer");
    }
    for (const char *key : {"state", "start_ms", "end_ms"})
    {
        const auto error = errorAfter(
            [key](Json::Value &b) { b["segments"][1].removeMember(key); });
        EXPECT_EQ(error.rfind(std::string("segments[1].") + key, 0), 0u)
            << key << ": " << error;
    }
}

TEST(SessionUploadTest, RejectsWrongTypes)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {R"({"client_session_id": 42})", "client_session_id"},
        {R"({"client_session_id": "not-a-uuid"})", "client_session_id"},
        {R"({"started_at": 1792152000})", "started_at"},
        {R"({"ended_at": "2026-10-16T22:00:00"})", "ended_at"},
        {R"({"threshold_version": "1"})", "threshold_version"},
        {R"({"threshold_version": 1.0})", "threshold_version"},
        {R"({"threshold_version": 1e0})", "threshold_version"},
        {R"({"threshold_version": true})", "threshold_version"},
        {R"({"app_version": 100})", "app_version"},
        {R"({"summary": []})", "summary"},
        {R"({"summary": {"study_sec": "3000"}})", "summary.study_sec"},
        {R"({"summary": {"study_sec": 3000.5}})", "summary.study_sec"},
        {R"({"summary": {"study_sec": null}})", "summary.study_sec"},
        {R"({"segments": {}})", "segments"},
        {R"({"segments": [1]})", "segments[0]"},
        {R"({"segments": [{"state": "normal", "start_ms": 0, "end_ms": 1}]})",
         "segments[0].state"},
        {R"({"segments": [{"state": "SLOUCH", "start_ms": 0, "end_ms": 1}]})",
         "segments[0].state"},
        {R"({"segments": [{"state": 0, "start_ms": 0, "end_ms": 1}]})",
         "segments[0].state"},
        {R"({"segments": [{"state": "FHP", "start_ms": "0", "end_ms": 1}]})",
         "segments[0].start_ms"},
        {R"({"segments": [{"state": "FHP", "start_ms": 0, "end_ms": 1,
                           "avg_ratio": "0.2"}]})",
         "segments[0].avg_ratio"},
        {R"({"segments": [{"state": "FHP", "start_ms": 0, "end_ms": 1,
                           "avg_angle": false}]})",
         "segments[0].avg_angle"},
    };
    for (const auto &[patch, field] : cases)
    {
        auto body = validBody();
        const auto changes = parseJson(patch);
        for (const auto &key : changes.getMemberNames())
        {
            if (changes[key].isObject() && key == "summary")
            {
                for (const auto &inner : changes[key].getMemberNames())
                    body[key][inner] = changes[key][inner];
            }
            else
            {
                body[key] = changes[key];
            }
        }
        const auto error = errorFor(body);
        EXPECT_EQ(error.rfind(field, 0), 0u) << patch << ": " << error;
    }
}

TEST(SessionUploadTest, RejectsNegativeAndOversizedNumbers)
{
    EXPECT_EQ(errorAfter([](Json::Value &b) { b["summary"]["fhp_sec"] = -1; }),
              "summary.fhp_sec must be between 0 and 2147483647");
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["summary"]["alert_count"] = Json::Int64{2147483648LL};
              }),
              "summary.alert_count must be between 0 and 2147483647");
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["summary"]["alert_count"] = Json::UInt64{1} << 63;
              }),
              "summary.alert_count must be between 0 and 2147483647");
    EXPECT_EQ(errorAfter([](Json::Value &b) { b["threshold_version"] = 0; }),
              "threshold_version must be between 1 and 2147483647");
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["segments"][0]["start_ms"] = -5;
              }),
              "segments[0].start_ms must be between 0 and 2147483647");
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["segments"][0]["avg_ratio"] = 1e39;
              }),
              "segments[0].avg_ratio is out of range");
}

TEST(SessionUploadTest, RejectsBadTimeRanges)
{
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["ended_at"] = "2026-10-16T20:59:59+09:00";
              }),
              "ended_at must not be before started_at");
    // The same wall clock time in another zone is earlier, not later.
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["ended_at"] = "2026-10-16T22:00:00+12:00";
              }),
              "ended_at must not be before started_at");
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["started_at"] = "2026-10-15T21:59:59+09:00";
              }),
              "a session may last at most 86400 seconds");
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["started_at"] = "1999-12-31T23:59:59Z";
                  b["ended_at"] = "2000-01-01T00:59:59Z";
              }),
              "started_at must not be before 2000-01-01");
}

TEST(SessionUploadTest, RejectsSummariesLongerThanTheSession)
{
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["summary"]["study_sec"] = 3001;
                  b["summary"]["away_sec"] = 602;
              }),
              "summary.study_sec + summary.away_sec exceeds the session "
              "length");
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["summary"]["normal_sec"] = 3604;
              }),
              "summary.normal_sec + summary.fhp_sec + summary.lean_sec "
              "exceeds the session length");
}

TEST(SessionUploadTest, RejectsMisplacedSegments)
{
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["segments"][1]["end_ms"] = 1999999;
              }),
              "segments[1].end_ms must not be less than start_ms");
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["segments"][1]["start_ms"] = 1999999;
              }),
              "segments[1] starts before segments[0] ends");
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  std::swap(b["segments"][0], b["segments"][2]);
              }),
              "segments[1] starts before segments[0] ends");
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["segments"][3]["end_ms"] = 3600001;
              }),
              "segments[3].end_ms is past the end of the session");
}

TEST(SessionUploadTest, RejectsBadAppVersions)
{
    const std::string expected =
        "app_version must be 1 to 64 printable ASCII characters";
    EXPECT_EQ(errorAfter([](Json::Value &b) { b["app_version"] = ""; }),
              expected);
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["app_version"] = std::string(65, '1');
              }),
              expected);
    EXPECT_EQ(errorAfter([](Json::Value &b) { b["app_version"] = "1.0\n"; }),
              expected);
    EXPECT_EQ(errorAfter([](Json::Value &b) {
                  b["app_version"] = std::string(64, '1');
              }),
              "");
}

TEST(SessionUploadTest, BuildsArrayLiteralsForUnnest)
{
    const auto upload = parseSessionUpload(validBody());
    const auto arrays = posture::toSegmentArrays(upload.segments);

    EXPECT_EQ(arrays.states, "{NORMAL,FHP,LEAN,AWAY}");
    EXPECT_EQ(arrays.startMs, "{0,2000000,2700000,3000000}");
    EXPECT_EQ(arrays.endMs, "{2000000,2700000,3000000,3600000}");
    EXPECT_EQ(arrays.avgRatios, "{0.12,0.31,NULL,NULL}");
    EXPECT_EQ(arrays.avgAngles, "{8.5,NULL,NULL,NULL}");

    const auto empty = posture::toSegmentArrays({});
    EXPECT_EQ(empty.states, "{}");
    EXPECT_EQ(empty.avgRatios, "{}");
}

TEST(SessionUploadTest, MapsPostureStateNames)
{
    for (const auto state : {PostureState::Normal,
                             PostureState::Fhp,
                             PostureState::Lean,
                             PostureState::Away})
    {
        EXPECT_EQ(posture::parsePostureState(posture::postureStateName(state)),
                  state);
    }
    EXPECT_FALSE(posture::parsePostureState("Normal"));
    EXPECT_FALSE(posture::parsePostureState(""));
}

TEST(SessionUploadTest, ParsesSessionIds)
{
    EXPECT_EQ(posture::parseSessionId("1"), 1);
    EXPECT_EQ(posture::parseSessionId("9876543210"), 9876543210LL);
    EXPECT_EQ(posture::parseSessionId("999999999999999999"),
              999999999999999999LL);
    for (const char *text : {"",
                             "0",
                             "007",
                             "-1",
                             "+1",
                             "1.0",
                             "1e3",
                             " 1",
                             "abc",
                             "1000000000000000000"})
    {
        EXPECT_FALSE(posture::parseSessionId(text)) << "text: '" << text << "'";
    }
}
