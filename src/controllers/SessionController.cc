#include "controllers/SessionController.h"

#include <drogon/drogon.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

#include "filters/JwtFilter.h"
#include "http/ErrorResponse.h"
#include "sessions/SessionUpload.h"
#include "stats/DailyStats.h"
#include "time/IsoTime.h"

namespace posture
{
namespace
{

using drogon::orm::DrogonDbException;
using drogon::orm::Result;
using drogon::orm::Row;
using Callback = std::function<void(const drogon::HttpResponsePtr &)>;
using CallbackPtr = std::shared_ptr<Callback>;
using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

// Columns every session response is built from. Instants come back as epoch
// microseconds so they can be formatted with an explicit +09:00 offset.
const std::string kSessionColumns =
    "id, client_session_id::text AS client_session_id, "
    "(extract(epoch FROM started_at) * 1000000)::bigint AS started_us, "
    "(extract(epoch FROM ended_at) * 1000000)::bigint AS ended_us, "
    "study_sec, away_sec, normal_sec, fhp_sec, lean_sec, alert_count, "
    "threshold_version, app_version";

drogon::HttpResponsePtr invalidRequest(const std::string &message)
{
    return makeErrorResponse(
        drogon::k400BadRequest, "INVALID_REQUEST", message);
}

drogon::HttpResponsePtr sessionNotFound()
{
    return makeErrorResponse(
        drogon::k404NotFound, "SESSION_NOT_FOUND", "session not found");
}

drogon::HttpResponsePtr databaseUnavailable()
{
    return makeErrorResponse(drogon::k503ServiceUnavailable,
                             "DATABASE_UNAVAILABLE",
                             "database is not reachable");
}

std::function<void(const DrogonDbException &)> onDbError(CallbackPtr respond,
                                                         const char *what)
{
    return [respond = std::move(respond), what](const DrogonDbException &e) {
        LOG_ERROR << what << " failed: " << e.base().what();
        (*respond)(databaseUnavailable());
    };
}

Json::Value sessionJson(const Row &row)
{
    Json::Value session;
    session["session_id"] = row["id"].as<Json::Int64>();
    session["client_session_id"] = row["client_session_id"].as<std::string>();
    session["started_at"] =
        formatSeoulTimestamp(row["started_us"].as<std::int64_t>());
    session["ended_at"] =
        formatSeoulTimestamp(row["ended_us"].as<std::int64_t>());
    const auto &version = row["threshold_version"];
    session["threshold_version"] =
        version.isNull() ? Json::Value() : Json::Value(version.as<int>());
    const auto &appVersion = row["app_version"];
    session["app_version"] = appVersion.isNull()
                                 ? Json::Value()
                                 : Json::Value(appVersion.as<std::string>());

    auto &summary = session["summary"];
    for (const char *column : {"study_sec",
                               "away_sec",
                               "normal_sec",
                               "fhp_sec",
                               "lean_sec",
                               "alert_count"})
    {
        summary[column] = row[column].as<int>();
    }
    return session;
}

Json::Value segmentJson(const Row &row)
{
    const auto real = [&row](const char *column) {
        return row[column].isNull() ? Json::Value()
                                    : Json::Value(row[column].as<double>());
    };

    Json::Value segment;
    segment["state"] = row["state"].as<std::string>();
    segment["start_ms"] = row["start_ms"].as<int>();
    segment["end_ms"] = row["end_ms"].as<int>();
    segment["avg_ratio"] = real("avg_ratio");
    segment["avg_angle"] = real("avg_angle");
    return segment;
}

// {"date", "study_sec", "normal_ratio"} from a daily_stats shaped row.
Json::Value dailyJson(const Row &row)
{
    const auto ratio = normalRatio(row["normal_sec"].as<std::int64_t>(),
                                   row["fhp_sec"].as<std::int64_t>(),
                                   row["lean_sec"].as<std::int64_t>());
    Json::Value daily;
    daily["date"] = row["stat_date"].as<std::string>();
    daily["study_sec"] = row["study_sec"].as<int>();
    daily["normal_ratio"] = ratio ? Json::Value(*ratio) : Json::Value();
    return daily;
}

// Runs `body` inside a transaction whose result is sent only after COMMIT
// succeeds. Steps put the success response in `onCommit`. Paths that roll
// back respond on their own; Drogon skips the commit callback for them.
struct TransactionReply
{
    CallbackPtr respond;
    drogon::HttpResponsePtr onCommit;
};
using ReplyPtr = std::shared_ptr<TransactionReply>;

void runTransaction(const ReplyPtr &reply,
                    std::function<void(const TransactionPtr &)> body)
{
    auto db = drogon::app().getDbClient();
    db->newTransactionAsync(
        [reply, body = std::move(body)](const TransactionPtr &trans) {
            if (!trans)
            {
                LOG_ERROR << "no database connection for a transaction";
                (*reply->respond)(databaseUnavailable());
                return;
            }
            trans->setCommitCallback([reply](bool committed) {
                if (committed && reply->onCommit)
                    (*reply->respond)(reply->onCommit);
                else
                    (*reply->respond)(databaseUnavailable());
            });
            body(trans);
        });
}

void abortWith(const TransactionPtr &trans,
               const ReplyPtr &reply,
               const drogon::HttpResponsePtr &response)
{
    trans->rollback();
    (*reply->respond)(response);
}

// The upload transaction, one step per statement:
//   lockUser -> insertSession -> insertSegments -> addToDailyStats
// with insertSession branching to findExisting on a retried upload.
//
// Concurrent uploads of the same (user_id, client_session_id) are serialized
// by the unique index: the second INSERT ... ON CONFLICT DO NOTHING waits for
// the first transaction and then inserts nothing, so the daily stats are
// only ever incremented once.
struct Upload
{
    std::string userId;
    SessionUpload session;
    ReplyPtr reply;
};
using UploadPtr = std::shared_ptr<const Upload>;

void findExisting(const TransactionPtr &trans, const UploadPtr &upload)
{
    trans->execSqlAsync(
        "SELECT s.id, "
        "to_char((s.started_at AT TIME ZONE 'Asia/Seoul')::date, "
        "'YYYY-MM-DD') AS stat_date, "
        "COALESCE(d.study_sec, 0) AS study_sec, "
        "COALESCE(d.normal_sec, 0) AS normal_sec, "
        "COALESCE(d.fhp_sec, 0) AS fhp_sec, "
        "COALESCE(d.lean_sec, 0) AS lean_sec "
        "FROM sessions s "
        "LEFT JOIN daily_stats d ON d.user_id = s.user_id "
        "AND d.stat_date = (s.started_at AT TIME ZONE 'Asia/Seoul')::date "
        "WHERE s.user_id = $1 AND s.client_session_id = $2",
        [trans, upload](const Result &result) {
            if (result.empty())
            {
                // The conflicting row was deleted between the two statements.
                abortWith(trans,
                          upload->reply,
                          makeErrorResponse(drogon::k409Conflict,
                                            "CONFLICT",
                                            "session changed during upload, "
                                            "retry the request"));
                return;
            }
            Json::Value body;
            body["session_id"] = result[0]["id"].as<Json::Int64>();
            body["daily"] = dailyJson(result[0]);
            upload->reply->onCommit =
                drogon::HttpResponse::newHttpJsonResponse(body);
        },
        onDbError(upload->reply->respond, "existing session lookup"),
        upload->userId,
        upload->session.clientSessionId);
}

void addToDailyStats(const TransactionPtr &trans,
                     const UploadPtr &upload,
                     std::int64_t sessionId)
{
    trans->execSqlAsync(
        "INSERT INTO daily_stats AS d (user_id, stat_date, study_sec, "
        "normal_sec, fhp_sec, lean_sec, session_count) "
        "SELECT user_id, (started_at AT TIME ZONE 'Asia/Seoul')::date, "
        "study_sec, normal_sec, fhp_sec, lean_sec, 1 "
        "FROM sessions WHERE id = $1 "
        "ON CONFLICT (user_id, stat_date) DO UPDATE SET "
        "study_sec = d.study_sec + EXCLUDED.study_sec, "
        "normal_sec = d.normal_sec + EXCLUDED.normal_sec, "
        "fhp_sec = d.fhp_sec + EXCLUDED.fhp_sec, "
        "lean_sec = d.lean_sec + EXCLUDED.lean_sec, "
        "session_count = d.session_count + 1, "
        "updated_at = now() "
        "RETURNING to_char(stat_date, 'YYYY-MM-DD') AS stat_date, "
        "study_sec, normal_sec, fhp_sec, lean_sec",
        [upload, sessionId](const Result &result) {
            Json::Value body;
            body["session_id"] = static_cast<Json::Int64>(sessionId);
            body["daily"] = dailyJson(result[0]);
            auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
            resp->setStatusCode(drogon::k201Created);
            resp->addHeader("Location",
                            "/api/v1/sessions/" + std::to_string(sessionId));
            upload->reply->onCommit = resp;
        },
        onDbError(upload->reply->respond, "daily stats upsert"),
        sessionId);
}

void insertSegments(const TransactionPtr &trans,
                    const UploadPtr &upload,
                    std::int64_t sessionId)
{
    if (upload->session.segments.empty())
    {
        addToDailyStats(trans, upload, sessionId);
        return;
    }

    auto arrays = toSegmentArrays(upload->session.segments);
    trans->execSqlAsync(
        "INSERT INTO posture_segments "
        "(session_id, state, start_ms, end_ms, avg_ratio, avg_angle) "
        "SELECT $1::bigint, s.state, s.start_ms, s.end_ms, s.avg_ratio, "
        "s.avg_angle "
        "FROM unnest($2::posture_state[], $3::int[], $4::int[], "
        "$5::real[], $6::real[]) "
        "AS s (state, start_ms, end_ms, avg_ratio, avg_angle)",
        [trans, upload, sessionId](const Result &) {
            addToDailyStats(trans, upload, sessionId);
        },
        onDbError(upload->reply->respond, "segment insert"),
        sessionId,
        std::move(arrays.states),
        std::move(arrays.startMs),
        std::move(arrays.endMs),
        std::move(arrays.avgRatios),
        std::move(arrays.avgAngles));
}

void insertSession(const TransactionPtr &trans, const UploadPtr &upload)
{
    const auto &s = upload->session;
    trans->execSqlAsync(
        "INSERT INTO sessions (user_id, client_session_id, started_at, "
        "ended_at, study_sec, away_sec, normal_sec, fhp_sec, lean_sec, "
        "alert_count, threshold_version, app_version) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12) "
        "ON CONFLICT (user_id, client_session_id) DO NOTHING "
        "RETURNING id",
        [trans, upload](const Result &result) {
            if (result.empty())
            {
                findExisting(trans, upload);
                return;
            }
            insertSegments(
                trans, upload, result[0]["id"].as<std::int64_t>());
        },
        onDbError(upload->reply->respond, "session insert"),
        upload->userId,
        s.clientSessionId,
        formatSeoulTimestamp(s.startedAtUs),
        formatSeoulTimestamp(s.endedAtUs),
        s.summary.studySec,
        s.summary.awaySec,
        s.summary.normalSec,
        s.summary.fhpSec,
        s.summary.leanSec,
        s.summary.alertCount,
        s.thresholdVersion,
        s.appVersion);
}

// Holds a KEY SHARE lock on the user row until commit so the user cannot be
// deleted halfway through, and checks the threshold version in the same
// round trip. The token filter is stateless, so a valid token can outlive
// its user.
void lockUser(const TransactionPtr &trans, const UploadPtr &upload)
{
    trans->execSqlAsync(
        "SELECT EXISTS (SELECT 1 FROM threshold_configs WHERE version = $2) "
        "AS known_version "
        "FROM users WHERE id = $1 FOR KEY SHARE OF users",
        [trans, upload](const Result &result) {
            if (result.empty())
            {
                auto resp = makeErrorResponse(drogon::k401Unauthorized,
                                              "UNAUTHORIZED",
                                              "user no longer exists");
                resp->addHeader("WWW-Authenticate", "Bearer");
                abortWith(trans, upload->reply, resp);
                return;
            }
            if (!result[0]["known_version"].as<bool>())
            {
                const auto version = upload->session.thresholdVersion;
                abortWith(trans,
                          upload->reply,
                          invalidRequest("unknown threshold_version " +
                                         std::to_string(version)));
                return;
            }
            insertSession(trans, upload);
        },
        onDbError(upload->reply->respond, "user lock"),
        upload->userId,
        upload->session.thresholdVersion);
}

// The removal transaction: delete the row, then subtract it from its day and
// drop the day once its last session is gone.
void removeEmptyDay(const TransactionPtr &trans,
                    const ReplyPtr &reply,
                    const std::string &userId,
                    const std::string &statDate)
{
    trans->execSqlAsync(
        "DELETE FROM daily_stats "
        "WHERE user_id = $1 AND stat_date = $2::date AND session_count <= 0",
        [](const Result &) {},
        onDbError(reply->respond, "empty daily stats delete"),
        userId,
        statDate);
}

void subtractFromDailyStats(const TransactionPtr &trans,
                            const ReplyPtr &reply,
                            const std::string &userId,
                            const Row &removed)
{
    auto statDate = removed["stat_date"].as<std::string>();
    trans->execSqlAsync(
        "UPDATE daily_stats SET "
        "study_sec = study_sec - $3::int, "
        "normal_sec = normal_sec - $4::int, "
        "fhp_sec = fhp_sec - $5::int, "
        "lean_sec = lean_sec - $6::int, "
        "session_count = session_count - 1, "
        "updated_at = now() "
        "WHERE user_id = $1 AND stat_date = $2::date "
        "RETURNING session_count",
        [trans, reply, userId, statDate](const Result &result) {
            if (result.empty())
            {
                LOG_WARN << "no daily_stats row for " << userId << " on "
                         << statDate << " while deleting a session";
            }
            else if (result[0]["session_count"].as<int>() <= 0)
            {
                removeEmptyDay(trans, reply, userId, statDate);
            }
        },
        onDbError(reply->respond, "daily stats subtract"),
        userId,
        statDate,
        removed["study_sec"].as<int>(),
        removed["normal_sec"].as<int>(),
        removed["fhp_sec"].as<int>(),
        removed["lean_sec"].as<int>());
}

}  // namespace

void SessionController::create(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    const auto json = req->getJsonObject();
    if (!json)
    {
        callback(invalidRequest("body must be a JSON object"));
        return;
    }

    auto upload = std::make_shared<Upload>();
    try
    {
        upload->session = parseSessionUpload(*json);
    }
    catch (const std::invalid_argument &e)
    {
        callback(invalidRequest(e.what()));
        return;
    }
    upload->userId = JwtFilter::userId(req);
    upload->reply = std::make_shared<TransactionReply>();
    upload->reply->respond = std::make_shared<Callback>(std::move(callback));

    runTransaction(upload->reply,
                   [upload = UploadPtr(upload)](const TransactionPtr &trans) {
                       lockUser(trans, upload);
                   });
}

void SessionController::list(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    const auto date = parseLocalDate(req->getParameter("date"));
    if (!date)
    {
        callback(invalidRequest("date must be a date in YYYY-MM-DD form"));
        return;
    }

    auto respond = std::make_shared<Callback>(std::move(callback));
    auto db = drogon::app().getDbClient();
    // A half open range on started_at so the (user_id, started_at) index
    // does the work.
    db->execSqlAsync(
        "SELECT " + kSessionColumns +
            " FROM sessions WHERE user_id = $1 "
            "AND started_at >= ($2::date)::timestamp AT TIME ZONE 'Asia/Seoul' "
            "AND started_at < ($2::date + 1)::timestamp AT TIME ZONE "
            "'Asia/Seoul' "
            "ORDER BY started_at, id",
        [respond, day = formatLocalDate(*date)](const Result &result) {
            Json::Value body;
            body["date"] = day;
            body["sessions"] = Json::Value(Json::arrayValue);
            for (const auto &row : result)
                body["sessions"].append(sessionJson(row));
            (*respond)(drogon::HttpResponse::newHttpJsonResponse(body));
        },
        onDbError(respond, "session list"),
        JwtFilter::userId(req),
        formatLocalDate(*date));
}

void SessionController::get(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    const std::string &id)
{
    const auto sessionId = parseSessionId(id);
    if (!sessionId)
    {
        callback(invalidRequest("session id must be a positive integer"));
        return;
    }

    auto respond = std::make_shared<Callback>(std::move(callback));
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT " + kSessionColumns +
            " FROM sessions WHERE id = $1 AND user_id = $2",
        [respond, db, sessionId = *sessionId](const Result &sessions) {
            if (sessions.empty())
            {
                (*respond)(sessionNotFound());
                return;
            }
            auto body = std::make_shared<Json::Value>(sessionJson(sessions[0]));
            db->execSqlAsync(
                "SELECT state::text AS state, start_ms, end_ms, avg_ratio, "
                "avg_angle FROM posture_segments WHERE session_id = $1 "
                "ORDER BY start_ms, id",
                [respond, body](const Result &segments) {
                    auto &list = (*body)["segments"];
                    list = Json::Value(Json::arrayValue);
                    for (const auto &row : segments)
                        list.append(segmentJson(row));
                    (*respond)(
                        drogon::HttpResponse::newHttpJsonResponse(*body));
                },
                onDbError(respond, "segment list"),
                sessionId);
        },
        onDbError(respond, "session lookup"),
        *sessionId,
        JwtFilter::userId(req));
}

void SessionController::remove(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    const std::string &id)
{
    const auto sessionId = parseSessionId(id);
    if (!sessionId)
    {
        callback(invalidRequest("session id must be a positive integer"));
        return;
    }

    auto reply = std::make_shared<TransactionReply>();
    reply->respond = std::make_shared<Callback>(std::move(callback));
    runTransaction(
        reply,
        [reply, userId = JwtFilter::userId(req), sessionId = *sessionId](
            const TransactionPtr &trans) {
            trans->execSqlAsync(
                "DELETE FROM sessions WHERE id = $1 AND user_id = $2 "
                "RETURNING to_char((started_at AT TIME ZONE 'Asia/Seoul')"
                "::date, 'YYYY-MM-DD') AS stat_date, "
                "study_sec, normal_sec, fhp_sec, lean_sec",
                [trans, reply, userId](const Result &result) {
                    if (result.empty())
                    {
                        abortWith(trans, reply, sessionNotFound());
                        return;
                    }
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k204NoContent);
                    reply->onCommit = resp;
                    subtractFromDailyStats(trans, reply, userId, result[0]);
                },
                onDbError(reply->respond, "session delete"),
                sessionId,
                userId);
        });
}

}  // namespace posture
