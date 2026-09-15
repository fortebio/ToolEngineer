-- Migration 2026-07-14: kho backup Drive (bảng drive_sessions), TÁCH khỏi 'sessions' (device POST).
-- Chạy:  sudo -u postgres psql mydb < ~/fbt_server/migrate_drive_sessions.sql
CREATE TABLE IF NOT EXISTS drive_sessions (
  id          bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  id_device   text NOT NULL,
  version     text,
  kit_id      text,
  type_upload text,
  method      text,
  received_at timestamptz NOT NULL DEFAULT now(),
  body_sha256 bytea NOT NULL UNIQUE,   -- dedup theo nội dung: mỗi bản đo Drive 1 dòng
  payload     jsonb NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_drive_device_time ON drive_sessions (id_device, received_at DESC);
GRANT SELECT, INSERT ON drive_sessions TO engineer;
