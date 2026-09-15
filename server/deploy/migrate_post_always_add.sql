-- Migration 2026-07-13: device POST LUÔN thêm hàng; Drive dedup theo nội dung.
-- Chạy 1 lần:  sudo -u postgres psql mydb < ~/fbt_server/migrate_post_always_add.sql
-- Bỏ ràng buộc UNIQUE(body_sha256) (cho phép POST lại tạo hàng trùng nội dung);
-- giữ index thường để lookup adopt/dedup Drive.
ALTER TABLE sessions DROP CONSTRAINT IF EXISTS sessions_body_sha256_key;
CREATE INDEX IF NOT EXISTS idx_sessions_sha ON sessions (body_sha256);
