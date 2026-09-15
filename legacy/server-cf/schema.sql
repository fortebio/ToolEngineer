-- Schema D1 (SQLite) — bản chuyển từ deploy/schema.sql (Postgres).
-- Chạy: npx wrangler d1 execute fbt --remote --file schema.sql
--
-- KHÁC BIỆT SO VỚI POSTGRES (cố ý, SQLite không có kiểu tương đương):
--   timestamptz  -> TEXT ISO-8601 UTC 'YYYY-MM-DDTHH:MM:SS.sssZ'. Luôn ghi dạng
--                   này thì so sánh chuỗi = so sánh thời gian, ORDER BY vẫn đúng.
--   jsonb        -> TEXT (JSON). Đọc bằng json_extract(); không index được sâu,
--                   nhưng dataset ~3.4k dòng nên không cần.
--   bytea sha256 -> TEXT hex.
--   text[] ids   -> TEXT chứa mảng JSON '["RPL01","*"]'.
--   IDENTITY     -> INTEGER PRIMARY KEY AUTOINCREMENT.
-- Không cần GRANT: D1 chỉ Worker truy cập được.

CREATE TABLE IF NOT EXISTS sessions (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  id_device   TEXT NOT NULL,
  version     TEXT,
  kit_id      TEXT,
  type_upload TEXT,
  method      TEXT,
  received_at TEXT NOT NULL,           -- thời điểm ĐO (payload 'time' > now())
  posted_at   TEXT,                    -- thời điểm thiết bị POST
  body_sha256 TEXT NOT NULL,
  payload     TEXT NOT NULL            -- JSON gốc đầy đủ
);
CREATE INDEX IF NOT EXISTS idx_sessions_device_time ON sessions (id_device, received_at DESC);
CREATE INDEX IF NOT EXISTS idx_sessions_time ON sessions (received_at DESC);
CREATE INDEX IF NOT EXISTS idx_sessions_sha ON sessions (body_sha256);

CREATE TABLE IF NOT EXISTS users (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  username    TEXT NOT NULL,
  password    TEXT NOT NULL,           -- 'pbkdf2$iter$salt$hash' | 'sha256$salt$hash' (legacy Sheet)
  role        TEXT NOT NULL DEFAULT 'user',
  ids         TEXT NOT NULL DEFAULT '[]',
  name        TEXT,
  active      INTEGER NOT NULL DEFAULT 1,
  email       TEXT,
  date_create TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
  fw_version  TEXT
);
-- UNIQUE theo lower(username): Postgres bản cũ để UNIQUE phân biệt hoa/thường trong khi
-- MỌI truy vấn dùng lower() → tạo được cả 'admin' lẫn 'Admin' rồi sửa/xoá trúng cả hai.
-- Sửa luôn ở đây, đừng bê lỗi cũ sang.
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_username_lower ON users (lower(username));

-- Bản firmware đang chọn cho OTA + các cấu hình lặt vặt khác (thay target.json trên đĩa).
CREATE TABLE IF NOT EXISTS settings (
  key   TEXT PRIMARY KEY,
  value TEXT
);
