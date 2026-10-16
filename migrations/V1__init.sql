-- Initial schema. Only numeric and time data is stored; no video, images or
-- personal identifiers. Deleting a user cascades to everything they own.

CREATE TYPE posture_state AS ENUM ('NORMAL', 'FHP', 'LEAN', 'AWAY');

CREATE TABLE users (
    id           UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    device_hash  TEXT        NOT NULL UNIQUE,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_seen_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE user_settings (
    user_id     UUID        PRIMARY KEY REFERENCES users (id) ON DELETE CASCADE,
    alert_mode  TEXT        NOT NULL DEFAULT 'SOUND'
                            CHECK (alert_mode IN ('SOUND', 'VIBRATE', 'OFF')),
    calib_ratio REAL,
    calib_angle REAL,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE threshold_configs (
    version      INT         PRIMARY KEY,
    t1_ratio     REAL        NOT NULL,
    t2_angle     REAL        NOT NULL,
    stable_sec   REAL        NOT NULL DEFAULT 2.0 CHECK (stable_sec > 0),
    analysis_fps INT         NOT NULL DEFAULT 5 CHECK (analysis_fps > 0),
    is_active    BOOLEAN     NOT NULL DEFAULT false,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- At most one config can be active at a time.
CREATE UNIQUE INDEX threshold_configs_single_active
    ON threshold_configs (is_active)
    WHERE is_active;

CREATE TABLE sessions (
    id                BIGSERIAL   PRIMARY KEY,
    user_id           UUID        NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    client_session_id UUID        NOT NULL,
    started_at        TIMESTAMPTZ NOT NULL,
    ended_at          TIMESTAMPTZ NOT NULL,
    study_sec         INT         NOT NULL CHECK (study_sec >= 0),
    away_sec          INT         NOT NULL DEFAULT 0 CHECK (away_sec >= 0),
    normal_sec        INT         NOT NULL DEFAULT 0 CHECK (normal_sec >= 0),
    fhp_sec           INT         NOT NULL DEFAULT 0 CHECK (fhp_sec >= 0),
    lean_sec          INT         NOT NULL DEFAULT 0 CHECK (lean_sec >= 0),
    alert_count       INT         NOT NULL DEFAULT 0 CHECK (alert_count >= 0),
    threshold_version INT         REFERENCES threshold_configs (version),
    app_version       TEXT,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT now(),
    CHECK (ended_at >= started_at),
    -- Scoped per user so a retried upload can only ever match its own owner.
    UNIQUE (user_id, client_session_id)
);

CREATE INDEX sessions_user_started_at_idx ON sessions (user_id, started_at);

CREATE TABLE posture_segments (
    id         BIGSERIAL     PRIMARY KEY,
    session_id BIGINT        NOT NULL REFERENCES sessions (id) ON DELETE CASCADE,
    state      posture_state NOT NULL,
    start_ms   INT           NOT NULL CHECK (start_ms >= 0),
    end_ms     INT           NOT NULL,
    avg_ratio  REAL,
    avg_angle  REAL,
    CHECK (end_ms >= start_ms)
);

CREATE INDEX posture_segments_session_id_idx ON posture_segments (session_id);

-- One row per user per local (Asia/Seoul) day, maintained with
-- INSERT ... ON CONFLICT when a session is saved.
CREATE TABLE daily_stats (
    user_id       UUID        NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    stat_date     DATE        NOT NULL,
    study_sec     INT         NOT NULL DEFAULT 0,
    normal_sec    INT         NOT NULL DEFAULT 0,
    fhp_sec       INT         NOT NULL DEFAULT 0,
    lean_sec      INT         NOT NULL DEFAULT 0,
    session_count INT         NOT NULL DEFAULT 0,
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (user_id, stat_date)
);
