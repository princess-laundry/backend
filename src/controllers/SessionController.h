#pragma once

#include <drogon/HttpController.h>

#include <string>

namespace posture
{

// Study sessions recorded by the app. Every route needs a bearer token and
// only ever sees the caller's own sessions.
class SessionController : public drogon::HttpController<SessionController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SessionController::create,
                  "/api/v1/sessions",
                  drogon::Post,
                  "posture::JwtFilter");
    ADD_METHOD_TO(SessionController::list,
                  "/api/v1/sessions",
                  drogon::Get,
                  "posture::JwtFilter");
    ADD_METHOD_TO(SessionController::get,
                  "/api/v1/sessions/{id}",
                  drogon::Get,
                  "posture::JwtFilter");
    ADD_METHOD_TO(SessionController::remove,
                  "/api/v1/sessions/{id}",
                  drogon::Delete,
                  "posture::JwtFilter");
    METHOD_LIST_END

    // Stores a session summary with its segments and adds it to the daily
    // stats, all in one transaction. 201 for a new session; 200 with the
    // stored session when (user, client_session_id) was uploaded before, in
    // which case nothing changes.
    void create(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // ?date=YYYY-MM-DD. Sessions that started on that Seoul calendar day.
    void list(const drogon::HttpRequestPtr &req,
              std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // One session with its segments. 404 unless the caller owns it.
    void get(const drogon::HttpRequestPtr &req,
             std::function<void(const drogon::HttpResponsePtr &)> &&callback,
             const std::string &id);

    // Deletes a session and takes its numbers back out of the daily stats in
    // the same transaction. 204 on success, 404 unless the caller owns it.
    void remove(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                const std::string &id);
};

}  // namespace posture
