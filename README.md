# Posture Backend

REST backend for the side view posture tracking study app. The Android app does all pose estimation on device and only uploads session summaries (study time, posture time, posture segments). This server stores those numbers, serves daily statistics, and hands out the active threshold config. It never receives video, images or landmarks.

Stack: C++17, Drogon 1.9.13, PostgreSQL 16, Nginx, CMake with vcpkg, GoogleTest, Docker Compose.

Android developers who only need a server to test against: follow the Korean step by step guide in [docs/local-server-guide.md](docs/local-server-guide.md).

## Running with Docker

```bash
cp .env.example .env
```

Set `POSTGRES_PASSWORD` and `JWT_SECRET` in `.env` (generate the secret with `openssl rand -hex 32`), then:

```bash
docker compose up --build
```

```bash
curl -i http://localhost:8080/api/v1/health
```

Compose starts five services: `db` (PostgreSQL), `migrate` (applies SQL migrations and exits), `server` (Drogon on port 8080 inside the network), `docs` (Swagger UI) and `nginx` (published on `HTTP_PORT`). Neither Drogon nor Swagger UI is exposed to the host; all traffic goes through Nginx.

The first image build compiles Drogon and libpq through vcpkg and takes several minutes. Later builds reuse the dependency layer and the vcpkg binary cache unless `vcpkg.json` changes.

To check the session and stats endpoints end to end once the stack is up (needs `curl` and `jq`):

```bash
./scripts/smoke-test.sh
```

The script signs in a fresh device, uploads ten sample sessions (including a retry, five concurrent copies of one upload, a session that crosses Seoul midnight and one written in UTC), and checks `/api/v1/stats/daily` against totals it computes from its own payloads. It also covers listing, detail, delete, ownership and validation errors, and exits non zero if any check fails. Pass another base URL as the first argument to run it elsewhere.

## Building locally

Requires CMake 3.21+, Ninja, a C++17 compiler and a vcpkg checkout with `VCPKG_ROOT` set.

```bash
cmake --preset dev
```

```bash
cmake --build --preset dev
```

```bash
ctest --preset dev
```

To run the binary against the Compose database:

```bash
docker compose up -d db migrate
```

```bash
DB_HOST=localhost DB_PORT=5433 DB_PASSWORD=<password from .env> JWT_SECRET=<secret from .env> ./build/dev/posture_server
```

## Configuration

The server reads everything from environment variables. Secrets are never stored in config files.

| Variable | Default | Notes |
| --- | --- | --- |
| `APP_LISTEN_ADDRESS` | `0.0.0.0` | |
| `APP_PORT` | `8080` | |
| `APP_THREADS` | `0` | IO threads, 0 means one per core |
| `APP_LOG_LEVEL` | `INFO` | TRACE, DEBUG, INFO, WARN, ERROR, FATAL |
| `DB_HOST` | `localhost` | |
| `DB_PORT` | `5432` | |
| `DB_NAME` | `posture` | |
| `DB_USER` | `posture` | |
| `DB_PASSWORD` | | Required |
| `DB_POOL_SIZE` | `4` | Connections in the pool |
| `JWT_SECRET` | | Required, at least 32 characters. HS256 signing key |
| `JWT_TTL_SEC` | `604800` | Token lifetime in seconds, 60 to 2592000 |

## Database migrations

Migrations live in `migrations/` as `V<number>__<name>.sql` and are applied in numeric order by `scripts/migrate.sh`. Applied versions are recorded in `schema_migrations`, and each file runs in a single transaction. Never edit a migration that has already been applied anywhere; add a new version instead.

To rerun migrations against a running database:

```bash
docker compose run --rm migrate
```

To wipe the local database and start over:

```bash
docker compose down -v
```

## Layout

| Path | Contents |
| --- | --- |
| `src/main.cc` | Startup, listener and database pool setup |
| `src/auth/` | Device id hashing and JWT issue and verify |
| `src/config/` | Environment based configuration |
| `src/controllers/` | HTTP controllers |
| `src/filters/` | Drogon filters (JWT check for protected routes) |
| `src/http/` | Shared response helpers |
| `src/sessions/` | Session upload validation |
| `src/stats/` | Normal ratio and stats date range rules |
| `src/time/` | ISO 8601 parsing and Seoul time formatting |
| `tests/` | GoogleTest unit tests |
| `migrations/` | Versioned SQL schema |
| `scripts/migrate.sh` | Migration runner used by the `migrate` service |
| `scripts/smoke-test.sh` | End to end check against the running Compose stack |
| `docker/Dockerfile` | Multi stage build, runs unit tests during the build |
| `docs/openapi.yaml` | OpenAPI spec served through Swagger UI |
| `nginx/default.conf` | Reverse proxy config |

## API documentation

The API is described in OpenAPI 3.0 at `docs/openapi.yaml`. With the stack running:

| URL | Contents |
| --- | --- |
| `http://localhost:8080/docs/` | Swagger UI. Use Authorize with a token from `POST /auth/device` to try protected endpoints |
| `http://localhost:8080/openapi.yaml` | The raw spec, for Postman import or client code generation |

Nginx serves the file straight from the repository, so edits show up on reload. Update the spec in the same change as any endpoint, request or response change. To lint it:

```bash
docker run --rm -v "$PWD/docs:/spec:ro" redocly/cli:2.67.0 lint /spec/openapi.yaml
```

## API conventions

All endpoints are under `/api/v1`. Requests and responses are UTF-8 JSON, timestamps are ISO 8601 with offset (`+09:00`), and errors use the HTTP status code with a body of the form `{"error":{"code":"...","message":"..."}}`.

| Method | Path | Auth | Status |
| --- | --- | --- | --- |
| GET | `/api/v1/health` | None | Implemented |
| POST | `/api/v1/auth/device` | None | Implemented |
| GET | `/api/v1/config` | Bearer | Implemented |
| POST | `/api/v1/sessions` | Bearer | Implemented |
| GET | `/api/v1/sessions?date=YYYY-MM-DD` | Bearer | Implemented |
| GET | `/api/v1/sessions/{id}` | Bearer | Implemented |
| DELETE | `/api/v1/sessions/{id}` | Bearer | Implemented |
| GET | `/api/v1/stats/daily?from=YYYY-MM-DD&to=YYYY-MM-DD` | Bearer | Implemented |
| DELETE | `/api/v1/users/me` | Bearer | Implemented |
| GET | `/api/v1/users/me/settings` | Bearer | Implemented |
| PUT | `/api/v1/users/me/settings` | Bearer | Implemented |

### Authentication

The app generates a random UUID on first install and keeps it on the device. It exchanges that id for an access token:

```bash
curl -s -X POST http://localhost:8080/api/v1/auth/device -H 'Content-Type: application/json' -d '{"device_id":"550e8400-e29b-41d4-a716-446655440000"}'
```

```json
{"access_token":"eyJ...","expires_in":604800,"token":"eyJ...","token_type":"Bearer","user_id":"7c9e6679-7425-40de-944b-e07fc1f90ae7"}
```

The server stores only the SHA-256 hash of the lowercased id in `users.device_hash`, never the id itself. The first call creates the user and later calls with the same id return the same `user_id` and update `last_seen_at`. The token is an HS256 JWT whose subject is the user id, signed with `JWT_SECRET` and valid for `JWT_TTL_SEC` seconds. When it expires the app calls this endpoint again with the same id. The id may also be sent as `device_uuid`, and `access_token` repeats `token`; both names exist for the Android client. A body without a UUID id gets 400 `INVALID_REQUEST`.

Protected endpoints need the header `Authorization: Bearer <token>`. A missing, malformed, tampered or expired token gets 401 with code `UNAUTHORIZED` and a `WWW-Authenticate: Bearer` header.

### Threshold config

```bash
curl -s http://localhost:8080/api/v1/config -H "Authorization: Bearer $TOKEN"
```

```json
{"analysis_fps":5,"stable_sec":2.0,"t1_delta":0.15,"t1_ratio":0.35,"t2_angle":20.0,"t2_delta":12.0,"version":1}
```

Returns the row of `threshold_configs` with `is_active = true`. Migration `V2__seed_threshold_config.sql` seeds version 1. To roll out new thresholds, add a migration that inserts the next version and moves `is_active` to it in the same transaction; sessions record which version they were classified with in `threshold_version`. If no row is active the endpoint returns 404 `CONFIG_NOT_FOUND`. `t1_delta` and `t2_delta` (migration V3) are the thresholds the app uses after a user's 5 second personal calibration; `null` means the app's built in values. A CHECK constraint keeps every threshold inside the ranges the app accepts, since the app refuses to build its estimator from anything else.

### Sessions

The app uploads one summary per study session when the session ends:

```bash
curl -s -X POST http://localhost:8080/api/v1/sessions -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' -d @session.json
```

```json
{
  "client_session_id": "3f2504e0-4f89-11d3-9a0c-0305e82c3301",
  "started_at": "2026-10-16T21:00:00+09:00",
  "ended_at": "2026-10-16T22:00:00+09:00",
  "threshold_version": 1,
  "app_version": "1.0.0",
  "summary": {"study_sec": 3000, "away_sec": 600, "normal_sec": 2000, "fhp_sec": 700, "lean_sec": 300, "alert_count": 4},
  "segments": [
    {"state": "NORMAL", "start_ms": 0, "end_ms": 2000000, "avg_ratio": 0.12, "avg_angle": 8.5},
    {"state": "FHP", "start_ms": 2000000, "end_ms": 2700000, "avg_ratio": 0.31, "avg_angle": 24.0},
    {"state": "LEAN", "start_ms": 2700000, "end_ms": 3000000},
    {"state": "AWAY", "start_ms": 3000000, "end_ms": 3600000}
  ]
}
```

A new session gets 201 with a `Location` header and the updated day it was counted on:

```json
{"daily":{"date":"2026-10-16","normal_ratio":0.666666666666667,"study_sec":3000},"session_id":122}
```

`client_session_id` is generated by the app and makes the upload idempotent. If the same user already uploaded that id, nothing is written and the response is 200 with the same body shape, carrying the stored `session_id` and the current numbers for its day. The app can therefore retry any failed or timed out upload. Concurrent copies of one upload are serialized by the `(user_id, client_session_id)` unique index, so only one of them is ever counted.

The session row, its segments and the daily stats update are written in one transaction, and the response is sent only after it commits. The day is `started_at` in Asia/Seoul, so a session that runs past midnight counts entirely towards the day it started. `daily_stats` gains the session's `study_sec`, `normal_sec`, `fhp_sec` and `lean_sec` and one more `session_count`.

Validation failures get 400 `INVALID_REQUEST` with a message naming the field. The rules, implemented in `src/sessions/SessionUpload.cc`:

| Field | Rule |
| --- | --- |
| `client_session_id` | UUID string |
| `started_at`, `ended_at` | ISO 8601 with `Z` or a `+HH:MM` offset, seconds optional, up to 9 fraction digits. `ended_at >= started_at`, at most 24 hours apart, not before 2000 |
| `threshold_version` | Positive integer naming a row in `threshold_configs` |
| `app_version` | 1 to 64 printable ASCII characters |
| `summary.*` | Non-negative 32 bit integers written without a fraction or exponent. `study_sec + away_sec` and `normal_sec + fhp_sec + lean_sec` may each exceed the session length by at most one second per term, to allow for rounding |
| `segments` | Array, possibly empty, ordered by `start_ms` and not overlapping |
| `segments[].state` | `NORMAL`, `FHP`, `LEAN` or `AWAY` |
| `segments[].start_ms`, `end_ms` | Integers with `0 <= start_ms <= end_ms`, ending no later than the session |
| `segments[].avg_ratio`, `avg_angle` | Optional, finite numbers or null |

Members the server does not know are ignored. A token whose user no longer exists gets 401 `UNAUTHORIZED`.

`GET /api/v1/sessions?date=2026-10-16` lists the caller's sessions that started on that Seoul day, oldest first:

```json
{"date":"2026-10-16","sessions":[{"app_version":"1.0.0","client_session_id":"3f2504e0-4f89-11d3-9a0c-0305e82c3301","ended_at":"2026-10-16T22:00:00+09:00","session_id":122,"started_at":"2026-10-16T21:00:00+09:00","summary":{"alert_count":4,"away_sec":600,"fhp_sec":700,"lean_sec":300,"normal_sec":2000,"study_sec":3000},"threshold_version":1}]}
```

`GET /api/v1/sessions/{id}` returns the same object plus `segments`, with `avg_ratio` and `avg_angle` set to null where the upload left them out. Times are always returned in Seoul time with a `+09:00` offset, whatever offset was uploaded. `DELETE /api/v1/sessions/{id}` removes the session and its segments and subtracts its numbers from `daily_stats` in the same transaction; a day whose last session is deleted disappears from the stats. It returns 204. Both return 404 `SESSION_NOT_FOUND` for ids that do not exist or belong to another user, and 400 for an id that is not a positive integer.

### Daily stats

```bash
curl -s 'http://localhost:8080/api/v1/stats/daily?from=2026-10-01&to=2026-10-31' -H "Authorization: Bearer $TOKEN"
```

```json
{"days":[{"date":"2026-10-16","fhp_sec":700,"lean_sec":300,"normal_ratio":0.666666666666667,"normal_sec":2000,"session_count":1,"study_sec":3000}],"from":"2026-10-01","to":"2026-10-31"}
```

`from` and `to` are inclusive Seoul dates. The range may cover at most 366 days, and an inverted, missing or malformed range gets 400 `INVALID_REQUEST`. Only days with at least one session are listed, in date order. `normal_ratio` is `normal_sec / (normal_sec + fhp_sec + lean_sec)` and is null when that sum is 0, for example on a day where the user was only ever away.
