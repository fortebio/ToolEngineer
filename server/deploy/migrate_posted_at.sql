-- Migration 2026-07-13: chống trùng theo NỘI DUNG + thêm posted_at.
-- Chạy 1 lần:  sudo -u postgres psql mydb -f ~/fbt_server/migrate_posted_at.sql
-- (Đảo lại thay đổi composite (body_sha256, received_at) trước đó.)

-- 1. Dọn dòng trùng nội dung (giữ id nhỏ nhất mỗi body_sha256) — bắt buộc trước khi thêm UNIQUE
DELETE FROM sessions a USING sessions b
WHERE a.body_sha256 = b.body_sha256 AND a.id > b.id;

-- 2. Thêm cột posted_at (thời điểm thiết bị POST; NULL nếu chỉ có từ Drive)
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS posted_at timestamptz;

-- 3. Đổi ràng buộc: bỏ composite -> unique theo nội dung
ALTER TABLE sessions DROP CONSTRAINT IF EXISTS sessions_dedup;
ALTER TABLE sessions ADD CONSTRAINT sessions_body_sha256_key UNIQUE (body_sha256);
