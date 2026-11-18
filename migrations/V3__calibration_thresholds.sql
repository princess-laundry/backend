-- Thresholds for personal calibration. When the app has measured a user's
-- upright baseline, it judges by how far the user moves from it:
-- head ratio more than t1_delta above the baseline counts as FHP, torso angle
-- more than t2_delta degrees above it counts as LEAN. NULL means the app
-- falls back to its built in values.
ALTER TABLE threshold_configs
    ADD COLUMN t1_delta REAL,
    ADD COLUMN t2_delta REAL;

-- Version 1 gets the app's built in values, so GET /config states them
-- explicitly instead of relying on the fallback.
UPDATE threshold_configs SET t1_delta = 0.15, t2_delta = 12.0 WHERE version = 1;

-- The app validates every threshold with require() and fails to build its
-- estimator on anything outside these ranges, so the database refuses such
-- rows instead of letting GET /config hand them out.
ALTER TABLE threshold_configs
    ADD CONSTRAINT threshold_configs_app_ranges CHECK (
        t1_ratio BETWEEN 0.05 AND 2.0
        AND t2_angle BETWEEN 1.0 AND 89.0
        AND stable_sec BETWEEN 0.2 AND 10.0
        AND analysis_fps BETWEEN 1 AND 10
        AND (t1_delta IS NULL OR t1_delta BETWEEN 0.01 AND 2.0)
        AND (t2_delta IS NULL OR t2_delta BETWEEN 1.0 AND 89.0)
    );
