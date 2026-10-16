# Posture Backend

REST backend for the side view posture tracking study app. The Android app does all pose estimation on device and only uploads session summaries (study time, posture time, posture segments). This server stores those numbers, serves daily statistics, and hands out the active threshold config. It never receives video, images or landmarks.

Stack: C++17, Drogon 1.9.13, PostgreSQL 16, Nginx, CMake with vcpkg, GoogleTest, Docker Compose.

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

Compose starts four services in order: `db` (PostgreSQL), `migrate` (applies SQL migrations and exits), `server` (Drogon on port 8080 inside the network) and `nginx` (published on `HTTP_PORT`). The Drogon port is not exposed to the host; all traffic goes through Nginx.

The first image build compiles Drogon and libpq through vcpkg and takes several minutes. Later builds reuse the dependency layer and the vcpkg binary cache unless `vcpkg.json` changes.

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
DB_HOST=localhost DB_PASSWORD=<password from .env> JWT_SECRET=<secret from .env> ./build/dev/posture_server
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
| `tests/` | GoogleTest unit tests |
| `migrations/` | Versioned SQL schema |
| `scripts/migrate.sh` | Migration runner used by the `migrate` service |
| `docker/Dockerfile` | Multi stage build, runs unit tests during the build |
| `nginx/default.conf` | Reverse proxy config |

## API conventions

All endpoints are under `/api/v1`. Requests and responses are UTF-8 JSON, timestamps are ISO 8601 with offset (`+09:00`), and errors use the HTTP status code with a body of the form `{"error":{"code":"...","message":"..."}}`.

| Method | Path | Auth | Status |
| --- | --- | --- | --- |
| GET | `/api/v1/health` | None | Implemented |
| POST | `/api/v1/auth/device` | None | Implemented |
| GET | `/api/v1/config` | Bearer | Implemented |

### Authentication

The app generates a random UUID on first install and keeps it on the device. It exchanges that id for an access token:

```bash
curl -s -X POST http://localhost:8080/api/v1/auth/device -H 'Content-Type: application/json' -d '{"device_id":"550e8400-e29b-41d4-a716-446655440000"}'
```

```json
{"expires_in":604800,"token":"eyJ...","token_type":"Bearer","user_id":"7c9e6679-7425-40de-944b-e07fc1f90ae7"}
```

The server stores only the SHA-256 hash of the lowercased id in `users.device_hash`, never the id itself. The first call creates the user and later calls with the same id return the same `user_id` and update `last_seen_at`. The token is an HS256 JWT whose subject is the user id, signed with `JWT_SECRET` and valid for `JWT_TTL_SEC` seconds. When it expires the app calls this endpoint again with the same id. A body without a UUID `device_id` gets 400 `INVALID_REQUEST`.

Protected endpoints need the header `Authorization: Bearer <token>`. A missing, malformed, tampered or expired token gets 401 with code `UNAUTHORIZED` and a `WWW-Authenticate: Bearer` header.

### Threshold config

```bash
curl -s http://localhost:8080/api/v1/config -H "Authorization: Bearer $TOKEN"
```

```json
{"analysis_fps":5,"stable_sec":2.0,"t1_ratio":0.25,"t2_angle":20.0,"version":1}
```

Returns the row of `threshold_configs` with `is_active = true`. Migration `V2__seed_threshold_config.sql` seeds version 1. To roll out new thresholds, add a migration that inserts the next version and moves `is_active` to it in the same transaction; sessions record which version they were classified with in `threshold_version`. If no row is active the endpoint returns 404 `CONFIG_NOT_FOUND`.
