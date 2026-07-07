const { Pool } = require('pg');

// Kết nối Postgres qua DATABASE_URL (đặt trong docker-compose).
const pool = new Pool({ connectionString: process.env.DATABASE_URL });

// Tạo bảng nếu chưa có — phòng khi init.sql không chạy (vd volume cũ đã tồn tại).
// Giữ ĐỒNG BỘ với db/init.sql.
async function initSchema() {
  await pool.query(`
    CREATE TABLE IF NOT EXISTS runs (
      id          BIGSERIAL   PRIMARY KEY,
      device_id   TEXT        NOT NULL,
      version     TEXT,
      run_time    TIMESTAMPTZ NOT NULL,
      kit_id      NUMERIC,
      raw         JSONB       NOT NULL,
      created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
      UNIQUE (device_id, run_time)
    );
    CREATE INDEX IF NOT EXISTS idx_runs_dev_time ON runs (device_id, run_time DESC);
  `);
}

module.exports = { pool, initSchema };
