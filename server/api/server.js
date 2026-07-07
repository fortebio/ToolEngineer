const express = require('express');
const { pool, initSchema } = require('./db');
const { parseRunTime, toAppRun } = require('./transform');

const app = express();
app.use(express.json({ limit: '5mb' })); // amplification (10 chuỗi) có thể lớn

const ADMIN_TOKEN = process.env.ADMIN_TOKEN || '';
const DEVICE_KEY = process.env.DEVICE_KEY || '';

// --- Healthcheck (cho Docker / cloudflared) ---
app.get('/health', (_req, res) => res.json({ ok: true }));

// --- Thiết bị POST kết quả ---
// Auth: header `X-Device-Key: <DEVICE_KEY>` hoặc `?key=` (firmware tiện hơn).
app.post('/ingest', async (req, res) => {
  const key = req.get('X-Device-Key') || req.query.key || '';
  if (!DEVICE_KEY || key !== DEVICE_KEY) {
    return res.status(401).json({ ok: false, error: 'bad device key' });
  }
  const b = req.body || {};
  const deviceId = b.id_device;
  if (!deviceId) return res.status(400).json({ ok: false, error: 'missing id_device' });
  // `time` có thể THIẾU (vd upload "Manual") hoặc sai định dạng → dùng giờ server
  // nhận. Lưu ý: khi đó mất tính idempotent theo time (mỗi POST = 1 bản ghi mới).
  let runTime = parseRunTime(b.time);
  if (Number.isNaN(runTime.getTime())) runTime = new Date();
  try {
    const r = await pool.query(
      `INSERT INTO runs (device_id, version, run_time, kit_id, raw)
       VALUES ($1, $2, $3, $4, $5)
       ON CONFLICT (device_id, run_time) DO UPDATE
         SET version = EXCLUDED.version, kit_id = EXCLUDED.kit_id, raw = EXCLUDED.raw
       RETURNING id`,
      [deviceId, b.version ?? null, runTime.toISOString(),
       Number.isFinite(Number(b.kitId)) ? Number(b.kitId) : null, b]
    );
    res.json({ ok: true, id: r.rows[0].id });
  } catch (e) {
    res.status(500).json({ ok: false, error: String(e) });
  }
});

// --- App đọc (admin) — Bearer token ---
function requireAdmin(req, res, next) {
  const h = req.get('Authorization') || '';
  const tok = h.startsWith('Bearer ') ? h.slice(7) : '';
  if (!ADMIN_TOKEN || tok !== ADMIN_TOKEN) {
    return res.status(401).json({ ok: false, error: 'unauthorized' });
  }
  next();
}

// Khớp hợp đồng Apps Script doGet mà Flutter CloudHistoryApi đang gọi.
app.get('/api', requireAdmin, async (req, res) => {
  const action = req.query.action;
  try {
    if (action === 'ids') {
      const r = await pool.query(`
        SELECT r.device_id AS id,
               COUNT(*)::int AS "runCount",
               MAX(r.run_time) AS latest,
               (SELECT version FROM runs r2 WHERE r2.device_id = r.device_id
                  ORDER BY run_time DESC LIMIT 1) AS version
          FROM runs r
         GROUP BY r.device_id
         ORDER BY latest DESC`);
      const devices = r.rows.map((x) => ({
        id: x.id,
        runCount: x.runCount,
        latest: x.latest ? new Date(x.latest).toISOString() : null,
        version: x.version || '',
      }));
      return res.json({ ok: true, devices });
    }

    if (action === 'runs') {
      const id = req.query.id;
      if (!id) return res.status(400).json({ ok: false, error: 'missing id' });
      const limit = Math.min(parseInt(req.query.limit, 10) || 10, 100);
      const offset = parseInt(req.query.offset, 10) || 0;
      const tot = await pool.query(
        'SELECT COUNT(*)::int AS n FROM runs WHERE device_id = $1', [id]);
      const r = await pool.query(
        `SELECT id, device_id, version, run_time, raw FROM runs
          WHERE device_id = $1 ORDER BY run_time DESC LIMIT $2 OFFSET $3`,
        [id, limit, offset]);
      return res.json({
        ok: true,
        runs: r.rows.map((row) => toAppRun(row, false)), // summary: KHÔNG kèm curves
        total: tot.rows[0].n,
        offset,
        limit,
      });
    }

    if (action === 'run') {
      const fileId = req.query.fileId;
      if (!fileId || !/^\d+$/.test(String(fileId))) {
        return res.status(400).json({ ok: false, error: 'missing/invalid fileId' });
      }
      const r = await pool.query(
        'SELECT id, device_id, version, run_time, raw FROM runs WHERE id = $1', [fileId]);
      if (r.rowCount === 0) return res.status(404).json({ ok: false, error: 'not found' });
      return res.json({ ok: true, run: toAppRun(r.rows[0], true) }); // kèm curves
    }

    return res.status(400).json({ ok: false, error: 'unknown action' });
  } catch (e) {
    res.status(500).json({ ok: false, error: String(e) });
  }
});

const PORT = process.env.PORT || 3000;
initSchema()
  .then(() => app.listen(PORT, () => console.log(`FBT_RAPID API nghe cổng ${PORT}`)))
  .catch((e) => {
    console.error('initSchema lỗi:', e);
    process.exit(1);
  });
