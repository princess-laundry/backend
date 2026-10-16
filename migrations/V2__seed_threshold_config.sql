-- First threshold config served by GET /api/v1/config. These are starting
-- values for the pilot study. Tuned thresholds go in as a new version row
-- that takes over is_active; never edit a version that apps may have used,
-- because sessions.threshold_version points at it.

INSERT INTO threshold_configs (version, t1_ratio, t2_angle, stable_sec, analysis_fps, is_active)
VALUES (1, 0.25, 20.0, 2.0, 5, true);
