-- Threshold version 2 from the first recording round (3 people, 4 takes).
-- The final estimator judges by the pelvis to ear line, so t2_angle (15
-- degrees) is the threshold that matters; t1_ratio is no longer used for
-- judging and keeps 0.35 because the column is NOT NULL. Personal
-- calibration is not used, so the delta columns stay NULL and the app falls
-- back to its built in values. Version 1 is left as it was and only loses
-- is_active, so sessions recorded with it keep pointing at the right values.
INSERT INTO threshold_configs (version, t1_ratio, t2_angle, stable_sec, analysis_fps, is_active)
VALUES (2, 0.35, 15.0, 2.0, 5, false);

UPDATE threshold_configs SET is_active = false WHERE is_active;
UPDATE threshold_configs SET is_active = true WHERE version = 2;
