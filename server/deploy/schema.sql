-- Schema cho dữ liệu phiên đo thiết bị (chạy 1 lần):
--   sudo -u postgres psql mydb -f schema.sql
CREATE TABLE IF NOT EXISTS sessions (
  id          bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  id_device   text NOT NULL,
  version     text,
  kit_id      text,
  type_upload text,
  method      text,
  received_at timestamptz NOT NULL DEFAULT now(),  -- thời điểm ĐO (payload 'time' > tên file > now())
  posted_at   timestamptz,                         -- thời điểm thiết bị POST (NULL nếu chỉ có từ Drive)
  body_sha256 bytea NOT NULL,  -- mã băm nội dung (KHÔNG unique: POST lại tạo hàng mới; Drive dedup theo cột này)
  payload     jsonb NOT NULL          -- bản gốc đầy đủ
);

CREATE INDEX IF NOT EXISTS idx_sessions_device_time ON sessions (id_device, received_at DESC);
CREATE INDEX IF NOT EXISTS idx_sessions_sha ON sessions (body_sha256);  -- cho lookup adopt/dedup Drive

-- Service chạy user 'engineer' → peer auth qua unix socket, không cần mật khẩu
DO $$ BEGIN
  CREATE ROLE engineer LOGIN;
EXCEPTION WHEN duplicate_object THEN NULL;
END $$;

GRANT SELECT, INSERT, UPDATE ON sessions TO engineer;

-- Tài khoản đăng nhập app Flutter (mật khẩu băm scrypt, không lưu thô)
CREATE TABLE IF NOT EXISTS users (
  id          bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  username    text NOT NULL UNIQUE,
  password    text NOT NULL,
  role        text NOT NULL DEFAULT 'user',
  ids         text[] NOT NULL DEFAULT '{}',  -- danh sách id thiết bị account được xem
  name        text,
  active      boolean NOT NULL DEFAULT true,
  email       text,
  date_create timestamptz NOT NULL DEFAULT now(),
  fw_version  text
);
-- DELETE: màn Quản lý User trong app (action deleteUser qua POST /auth) xoá tài khoản
GRANT SELECT, INSERT, UPDATE, DELETE ON users TO engineer;
-- username KHÔNG phân biệt hoa/thường (mọi truy vấn db.py dùng lower(username), nhưng
-- UNIQUE ở cột thì phân biệt → tạo được cả 'admin' lẫn 'Admin' rồi sửa/xoá trúng cả hai).
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_username_lower ON users (lower(username));

-- Kho BACKUP dữ liệu từ Google Drive (TÁCH khỏi 'sessions' của device POST).
-- Dedup theo NỘI DUNG (body_sha256 UNIQUE) -> mỗi bản đo Drive 1 dòng, import lại an toàn.
CREATE TABLE IF NOT EXISTS drive_sessions (
  id          bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  id_device   text NOT NULL,
  version     text,
  kit_id      text,
  type_upload text,
  method      text,
  received_at timestamptz NOT NULL DEFAULT now(),  -- thời điểm đo (payload 'time' > tên file > now())
  body_sha256 bytea NOT NULL UNIQUE,
  payload     jsonb NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_drive_device_time ON drive_sessions (id_device, received_at DESC);
GRANT SELECT, INSERT ON drive_sessions TO engineer;
