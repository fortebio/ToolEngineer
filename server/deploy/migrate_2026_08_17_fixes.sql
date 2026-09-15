-- Migration 2026-08-17: vá 2 lỗi bảng `users` trên box ĐANG CHẠY.
-- Chạy:  sudo -u postgres psql mydb -f ~/fbt_server/migrate_2026_08_17_fixes.sql
--
-- (schema.sql và migrate_users.sql đã được sửa cho box MỚI; file này để vá box CŨ
--  vốn tạo bảng trước khi có 2 thay đổi dưới đây.)

-- 1) Thiếu DELETE → app xoá tài khoản trả 500 "Lỗi máy chủ" chung chung.
GRANT SELECT, INSERT, UPDATE, DELETE ON users TO engineer;

-- 2) username phân biệt hoa/thường trong khi mọi truy vấn dùng lower(username).
--    KIỂM TRA TRƯỚC — nếu câu này trả ra dòng nào thì phải gộp/xoá thủ công,
--    vì CREATE UNIQUE INDEX bên dưới sẽ FAIL (đúng: đừng im lặng xoá dữ liệu):
--
--      SELECT lower(username) AS ten, count(*), array_agg(username)
--        FROM users GROUP BY 1 HAVING count(*) > 1;
--
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_username_lower ON users (lower(username));

-- Xác nhận sau khi chạy:
--   \d users            -- phải thấy idx_users_username_lower
--   \dp users           -- phải thấy engineer=arwd (có d = DELETE)
