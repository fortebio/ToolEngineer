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
-- DELETE là BẮT BUỘC: màn Quản lý User trong app có action deleteUser. Thiếu nó thì
-- xoá tài khoản báo "Lỗi máy chủ" (500) chung chung, rất khó lần ra. (schema.sql có
-- sẵn DELETE; file này thiếu → box nào provision bằng migration là dính.)
GRANT SELECT, INSERT, UPDATE, DELETE ON users TO engineer;

-- username KHÔNG phân biệt hoa/thường: mọi truy vấn trong db.py dùng lower(username),
-- nhưng UNIQUE ở trên lại phân biệt → tạo được CẢ 'admin' lẫn 'Admin', rồi
-- set_password/delete_user tác động lên cả hai dòng còn get_user lấy đại một dòng.
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_username_lower ON users (lower(username));
