# Posture Backend

REST backend for the side view posture tracking study app. The Android app does all pose estimation on device and only uploads session summaries (study time, posture time, posture segments). This server stores those numbers, serves daily statistics, and hands out the active threshold config. It never receives video, images or landmarks.

Stack: C++17, Drogon 1.9.13, PostgreSQL 16, Nginx, CMake with vcpkg, GoogleTest, Docker Compose.

## Running with Docker

```bash
cp .env.example .env
```

Set `POSTGRES_PASSWORD` in `.env`, then:

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
DB_HOST=localhost DB_PASSWORD=<password from .env> ./build/dev/posture_server
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
| `src/config/` | Environment based configuration |
| `src/controllers/` | HTTP controllers |
| `tests/` | GoogleTest unit tests |
| `migrations/` | Versioned SQL schema |
| `scripts/migrate.sh` | Migration runner used by the `migrate` service |
| `docker/Dockerfile` | Multi stage build, runs unit tests during the build |
| `nginx/default.conf` | Reverse proxy config |

## API conventions

All endpoints are under `/api/v1`. Requests and responses are UTF-8 JSON, timestamps are ISO 8601 with offset (`+09:00`), and errors use the HTTP status code with a body of the form `{"error":{"code":"...","message":"..."}}`.

| Method | Path | Status |
| --- | --- | --- |
| GET | `/api/v1/health` | Implemented |
