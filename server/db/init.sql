-- Schema khởi tạo (chạy 1 lần khi volume Postgres còn trống).
-- Một bảng `runs`: lưu RAW JSON firmware (JSONB) + vài cột rút ra để index nhanh.
CREATE TABLE IF NOT EXISTS runs (
  id          BIGSERIAL   PRIMARY KEY,          -- = fileId mà app dùng ở action=run
  device_id   TEXT        NOT NULL,
  version     TEXT,
  run_time    TIMESTAMPTZ NOT NULL,             -- suy từ raw.time ("dd-MM-yyyy HH:mm:ss", coi UTC+7)
  kit_id      NUMERIC,
  raw         JSONB       NOT NULL,             -- toàn bộ JSON firmware (giữ outcome/peak_features/origins/LED_power)
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  UNIQUE (device_id, run_time)                  -- POST lại cùng máy+thời gian = cập nhật, không nhân bản
);

CREATE INDEX IF NOT EXISTS idx_runs_dev_time ON runs (device_id, run_time DESC);
