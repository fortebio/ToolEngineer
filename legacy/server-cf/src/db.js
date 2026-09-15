// Truy cập D1 — bản chuyển của server/app/db.py. Mọi SQL Postgres-only đã viết lại cho SQLite.
import { canonicalSha256Hex, isoUtc, parseAmplification } from './logic.js';

// --- Ghi phiên đo ------------------------------------------------------------

/**
 * Ghi 1 phiên đo. dedup=true (nạp bù) → bỏ qua nếu nội dung đã có, trả null.
 * Giữ nguyên hợp đồng insert_session của Python: mỗi POST thiết bị = 1 hàng mới.
 */
export async function insertSession(env, data, receivedAt, dedup = false) {
  const sha = await canonicalSha256Hex(data);
  if (dedup) {
    const hit = await env.DB.prepare('SELECT 1 FROM sessions WHERE body_sha256 = ? LIMIT 1')
      .bind(sha)
      .first();
    if (hit) return null;
  }
  const now = isoUtc();
  const row = await env.DB.prepare(
    `INSERT INTO sessions (id_device, version, kit_id, type_upload, method,
                           body_sha256, payload, received_at, posted_at)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id`,
  )
    .bind(
      String(data.id_device),
      data.version ?? null,
      String(data.kitId ?? '') || null,
      data.type_Upload ?? null,
      data.method ?? null,
      sha,
      JSON.stringify(data),
      receivedAt || now,
      now,
    )
    .first();
  return row?.id ?? null;
}

// --- Đọc ---------------------------------------------------------------------

/** Thiết bị + số phiên + lần gửi cuối + version firmware GẦN NHẤT CÓ ghi version. */
export async function listDevices(env) {
  const { results } = await env.DB.prepare(
    `SELECT s.id_device                          AS id_device,
            count(*)                             AS sessions,
            max(s.received_at)                   AS last_seen,
            (SELECT s2.version FROM sessions s2
              WHERE s2.id_device = s.id_device AND s2.version IS NOT NULL
              ORDER BY s2.received_at DESC LIMIT 1) AS version
       FROM sessions s GROUP BY s.id_device ORDER BY last_seen DESC`,
  ).all();
  return results ?? [];
}

/** WHERE động cho /sessions — trả [mệnh đề, tham số]. */
function sessionsWhere({ device, from, to }) {
  const cond = [];
  const args = [];
  if (device) {
    cond.push('id_device = ?');
    args.push(device);
  }
  if (from) {
    cond.push('received_at >= ?');
    args.push(`${from}T00:00:00.000Z`);
  }
  if (to) {
    // 'to' bao gồm cả ngày đó → mốc chặn là 00:00 ngày KẾ TIẾP (giống `to::date + 1`).
    const next = new Date(`${to}T00:00:00.000Z`);
    next.setUTCDate(next.getUTCDate() + 1);
    cond.push('received_at < ?');
    args.push(next.toISOString());
  }
  return [cond.length ? `WHERE ${cond.join(' AND ')}` : '', args];
}

export async function listSessions(env, { device, from, to, page, limit }) {
  const [where, args] = sessionsWhere({ device, from, to });
  const total = await env.DB.prepare(`SELECT count(*) AS n FROM sessions ${where}`)
    .bind(...args)
    .first();
  const { results } = await env.DB.prepare(
    `SELECT id, id_device, received_at, posted_at, version, type_upload,
            json_extract(payload, '$.CT_value') AS ct_value,
            json_extract(payload, '$.result')   AS result
       FROM sessions ${where}
      ORDER BY received_at DESC LIMIT ? OFFSET ?`,
  )
    .bind(...args, limit, (page - 1) * limit)
    .all();
  return {
    total: total?.n ?? 0,
    page,
    limit,
    // json_extract trả JSON dạng CHUỖI cho mảng → parse lại để app nhận đúng list.
    items: (results ?? []).map((r) => ({ ...r, ct_value: jsonOrNull(r.ct_value), result: jsonOrNull(r.result) })),
  };
}

function jsonOrNull(s) {
  if (s == null) return null;
  try {
    return JSON.parse(s);
  } catch {
    return null;
  }
}

/** Chi tiết 1 phiên, BỎ amplification (nặng — lấy ở endpoint riêng). */
export async function getSession(env, sid) {
  const row = await env.DB.prepare(
    `SELECT id, id_device, received_at, posted_at, version, type_upload, method,
            json_remove(payload, '$.amplification') AS payload
       FROM sessions WHERE id = ?`,
  )
    .bind(sid)
    .first();
  if (!row) return null;
  return { ...row, payload: jsonOrNull(row.payload) ?? {} };
}

export async function getAmplification(env, sid) {
  const row = await env.DB.prepare('SELECT payload FROM sessions WHERE id = ?').bind(sid).first();
  if (!row) return null;
  return parseAmplification(jsonOrNull(row.payload) ?? {});
}

// --- Tài khoản ---------------------------------------------------------------

const userFromRow = (r) =>
  r && { ...r, active: !!r.active, ids: jsonOrNull(r.ids) ?? [] };

export async function getUser(env, username) {
  const r = await env.DB.prepare(
    `SELECT username, password, role, ids, name, active, email, fw_version, date_create
       FROM users WHERE lower(username) = lower(?)`,
  )
    .bind(username)
    .first();
  return userFromRow(r);
}

export async function listUsers(env) {
  const { results } = await env.DB.prepare(
    `SELECT username, role, ids, name, active, email, fw_version, date_create
       FROM users ORDER BY username`,
  ).all();
  return (results ?? []).map(userFromRow);
}

/** Tạo mới / cập nhật. passwordHash|email = null → GIỮ giá trị cũ. Trả true nếu TẠO MỚI. */
export async function upsertUser(env, { username, role, ids, name, active, passwordHash, email }) {
  const existing = await env.DB.prepare('SELECT id FROM users WHERE lower(username) = lower(?)')
    .bind(username)
    .first();
  if (existing) {
    await env.DB.prepare(
      `UPDATE users SET username=?, role=?, ids=?, name=?, active=?,
              password = COALESCE(?, password), email = COALESCE(?, email)
        WHERE id=?`,
    )
      .bind(username, role, JSON.stringify(ids), name, active ? 1 : 0, passwordHash, email, existing.id)
      .run();
    return false;
  }
  await env.DB.prepare(
    `INSERT INTO users (username, password, role, ids, name, active, email)
     VALUES (?, ?, ?, ?, ?, ?, ?)`,
  )
    .bind(username, passwordHash ?? '', role, JSON.stringify(ids), name, active ? 1 : 0, email)
    .run();
  return true;
}

export async function deleteUser(env, username) {
  const r = await env.DB.prepare('DELETE FROM users WHERE lower(username) = lower(?)').bind(username).run();
  return (r.meta?.changes ?? 0) > 0;
}

export async function setPassword(env, username, passwordHash) {
  await env.DB.prepare('UPDATE users SET password = ? WHERE lower(username) = lower(?)')
    .bind(passwordHash, username)
    .run();
}

export async function setEmail(env, username, email) {
  await env.DB.prepare('UPDATE users SET email = ? WHERE lower(username) = lower(?)')
    .bind(email, username)
    .run();
}

export async function countRoots(env, activeOnly = false) {
  const sql = `SELECT count(*) AS n FROM users WHERE role = 'root'${activeOnly ? ' AND active = 1' : ''}`;
  const r = await env.DB.prepare(sql).first();
  return r?.n ?? 0;
}

// --- settings (thay target.json trên đĩa) ------------------------------------

export async function getSetting(env, key) {
  const r = await env.DB.prepare('SELECT value FROM settings WHERE key = ?').bind(key).first();
  return r?.value ?? null;
}

export async function setSetting(env, key, value) {
  if (value === null) {
    await env.DB.prepare('DELETE FROM settings WHERE key = ?').bind(key).run();
    return;
  }
  await env.DB.prepare(
    'INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value',
  )
    .bind(key, value)
    .run();
}
