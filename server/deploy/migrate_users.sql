-- Migration 2026-07-13: bảng tài khoản đăng nhập app Flutter.
-- Chạy:  sudo -u postgres psql mydb < ~/fbt_server/migrate_users.sql
CREATE TABLE IF NOT EXISTS users (
  id          bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  username    text NOT NULL UNIQUE,
  password    text NOT NULL,                 -- băm scrypt 'scrypt$salt$hash' (KHÔNG lưu thô)
  role        text NOT NULL DEFAULT 'user',
  ids         text[] NOT NULL DEFAULT '{}',  -- danh sách id thiết bị account được xem
  name        text,
  active      boolean NOT NULL DEFAULT true,
  email       text,
  date_create timestamptz NOT NULL DEFAULT now(),
  fw_version  text
);
GRANT SELECT, INSERT, UPDATE ON users TO engineer;
