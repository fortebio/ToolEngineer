// Worker thay FBT Home Server (FastAPI). GIỮ NGUYÊN hợp đồng HTTP để app Flutter
// và firmware không phải sửa gì ngoài URL. Đối chiếu: server/app/main.py
//
// Thứ tự khớp route quan trọng: path CỐ ĐỊNH phải xét TRƯỚC path có tham số
// (/ota/check trước /ota/{file}), và POST catch-all (ingest) xét CUỐI CÙNG.
import * as accounts from './auth.js';
import * as db from './db.js';
import { checkAuth, isoUtc, payloadTime, safeName, canonicalSha256Hex, validate } from './logic.js';

const MAX_BODY = 16 * 1024 * 1024; // chặn payload khổng lồ nuốt RAM
const OTA_PREFIX = 'ota/';
const DATA_PREFIX = 'data/';
const TARGET_KEY = 'ota_target';

const json = (data, status = 200) =>
  new Response(JSON.stringify(data), {
    status,
    headers: { 'content-type': 'application/json; charset=utf-8' },
  });
const fail = (status, detail) => json({ detail }, status);

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;
    const method = request.method;
    const authed = () => checkAuth(request.headers.get('authorization') ?? '', env.RECEIVER_TOKEN);
    const need = () => (authed() ? null : fail(401, 'unauthorized'));

    // --- không cần token ---
    if (method === 'GET' && path === '/') {
      return json({ ok: true, service: 'FBT Server (Cloudflare Worker)', docs: '/app/' });
    }
    // /auth KHÔNG gate Bearer (app web public không nhúng token được); mọi action tự
    // gate bằng mật khẩu. LUÔN trả 200 kèm cờ ok — hợp đồng Apps Script.
    if (method === 'POST' && path === '/auth') {
      let body;
      try {
        body = JSON.parse(await request.text());
      } catch {
        return json({ ok: false, error: 'JSON không hợp lệ' });
      }
      if (!body || typeof body !== 'object' || Array.isArray(body)) {
        return json({ ok: false, error: 'Body không hợp lệ' });
      }
      try {
        return json(await accounts.dispatch(env, body));
      } catch (e) {
        console.error('auth action failed:', e);
        return json({ ok: false, error: 'Lỗi máy chủ, vui lòng thử lại' });
      }
    }

    // --- đọc dữ liệu (Bearer) ---
    if (method === 'GET' && path === '/devices') {
      return need() ?? json(await db.listDevices(env));
    }

    if (method === 'GET' && path === '/sessions') {
      const blocked = need();
      if (blocked) return blocked;
      const q = url.searchParams;
      const from = q.get('from');
      const to = q.get('to');
      for (const [name, v] of [['from', from], ['to', to]]) {
        if (v !== null && !isYmd(v)) return fail(400, `${name} phải dạng YYYY-MM-DD`);
      }
      const page = Math.max(1, Number(q.get('page') ?? 1) || 1);
      const limit = Math.min(200, Math.max(1, Number(q.get('limit') ?? 20) || 20));
      return json(await db.listSessions(env, { device: q.get('device'), from, to, page, limit }));
    }

    const amp = /^\/sessions\/(\d+)\/amplification$/.exec(path);
    if (method === 'GET' && amp) {
      const blocked = need();
      if (blocked) return blocked;
      const slots = await db.getAmplification(env, Number(amp[1]));
      return slots === null ? fail(404, 'session not found') : json({ id: Number(amp[1]), slots });
    }

    const detail = /^\/sessions\/(\d+)$/.exec(path);
    if (method === 'GET' && detail) {
      const blocked = need();
      if (blocked) return blocked;
      const s = await db.getSession(env, Number(detail[1]));
      return s === null ? fail(404, 'session not found') : json(s);
    }

    // --- OTA ---
    if (path === '/ota' && method === 'GET') {
      const blocked = need();
      if (blocked) return blocked;
      const listing = await env.R2.list({ prefix: OTA_PREFIX });
      const files = listing.objects
        .map((o) => ({ name: o.key.slice(OTA_PREFIX.length), size: o.size, modified: o.uploaded }))
        .sort((a, b) => String(b.modified).localeCompare(String(a.modified)));
      return json({ target: await otaTarget(env), files });
    }

    if (path === '/ota/check' && method === 'GET') {
      const blocked = need();
      if (blocked) return blocked;
      const name = await otaTarget(env);
      if (!name) return json({ update: false });
      const obj = await env.R2.get(OTA_PREFIX + name);
      if (!obj) return json({ update: false });
      const bytes = await obj.arrayBuffer();
      return json({
        update: true,
        version: name,
        size: bytes.byteLength,
        sha256: await sha256Bytes(bytes),
        url: `${url.origin}/ota/${name}`,
      });
    }

    if (path === '/ota/target' && method === 'DELETE') {
      const blocked = need();
      if (blocked) return blocked;
      await db.setSetting(env, TARGET_KEY, null);
      return json({ ok: true, target: null });
    }

    const setTarget = /^\/ota\/target\/(.+)$/.exec(path);
    if (setTarget && method === 'PUT') {
      const blocked = need();
      if (blocked) return blocked;
      const name = otaName(setTarget[1]);
      if (!name) return fail(400, 'tên file phải là .bin hợp lệ');
      if (!(await env.R2.head(OTA_PREFIX + name))) return fail(404, 'firmware not found');
      await db.setSetting(env, TARGET_KEY, name);
      return json({ ok: true, target: name });
    }

    const otaFile = /^\/ota\/([^/]+)$/.exec(path);
    if (otaFile && (method === 'PUT' || method === 'DELETE' || method === 'GET')) {
      const blocked = need();
      if (blocked) return blocked;
      const raw = decodeURIComponent(otaFile[1]);
      const name = otaName(raw);

      if (method === 'GET') {
        if (!name) return fail(404, 'firmware not found');
        const obj = await env.R2.get(OTA_PREFIX + name);
        if (!obj) return fail(404, 'firmware not found');
        return new Response(obj.body, {
          headers: {
            'content-type': 'application/octet-stream',
            'content-disposition': `attachment; filename="${name}"`,
          },
        });
      }
      if (!name) return fail(400, 'tên file phải là .bin hợp lệ');

      if (method === 'DELETE') {
        await env.R2.delete(OTA_PREFIX + name);
        if ((await db.getSetting(env, TARGET_KEY)) === name) await db.setSetting(env, TARGET_KEY, null);
        return json({ ok: true });
      }
      // PUT = tải firmware lên, body là NGUYÊN bytes (không multipart).
      const sizeErr = checkLength(request);
      if (sizeErr) return sizeErr;
      const bytes = await request.arrayBuffer();
      if (bytes.byteLength === 0) return fail(400, 'file rỗng');
      await env.R2.put(OTA_PREFIX + name, bytes);
      return json({ ok: true, name, size: bytes.byteLength });
    }

    // --- ingest: BẮT MỌI path POST còn lại (firmware gửi vào path nào cũng nhận) ---
    if (method === 'POST') {
      const blocked = need();
      if (blocked) return blocked;
      const sizeErr = checkLength(request);
      if (sizeErr) return sizeErr;
      const raw = await request.text();
      if (!raw || raw.length > MAX_BODY) return fail(400, 'missing/too large body');
      let data;
      try {
        data = JSON.parse(raw);
      } catch (e) {
        return fail(400, `invalid json: ${e}`);
      }
      const err = validate(data);
      if (err) return fail(400, err);
      return ingest(env, data, raw);
    }

    return fail(404, 'not found');
  },
};

// --- helper ------------------------------------------------------------------

const isYmd = (s) => /^\d{4}-\d{2}-\d{2}$/.test(s) && !Number.isNaN(Date.parse(`${s}T00:00:00Z`));

/** Bắt buộc Content-Length: chặn chunked stream vô hạn + body quá lớn. */
function checkLength(request) {
  const cl = request.headers.get('content-length');
  if (!cl || !/^\d+$/.test(cl)) return fail(411, 'content-length required');
  if (Number(cl) > MAX_BODY) return fail(413, 'body too large');
  return null;
}

/** Tên .bin an toàn, hoặc null nếu bẩn/sai đuôi. */
function otaName(filename) {
  const name = safeName(filename);
  return name === filename && name.toLowerCase().endsWith('.bin') ? name : null;
}

async function otaTarget(env) {
  const name = await db.getSetting(env, TARGET_KEY);
  if (!name) return null;
  // Target trỏ file đã bị xoá → coi như chưa chọn (không để app hiện file ma).
  return (await env.R2.head(OTA_PREFIX + name)) ? name : null;
}

async function sha256Bytes(buf) {
  const d = await crypto.subtle.digest('SHA-256', buf);
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

/**
 * Ghi 1 phiên: R2 TRƯỚC (nguồn chân lý) rồi D1.
 *
 * KHÁC bản FastAPI ở đúng một chỗ — và là chỗ CỐ Ý sửa: bản cũ trả {ok:true} kể cả
 * khi ghi file LẪN insert DB đều hỏng, thiết bị tưởng gửi xong và không retry → mất
 * hẳn phiên đo. Ở đây cả hai đường ghi cùng hỏng thì trả 500 để firmware gửi lại.
 */
async function ingest(env, data, raw) {
  const device = safeName(String(data.id_device));
  const sha = await canonicalSha256Hex(data);
  const day = isoUtc().slice(0, 10).replace(/-/g, '');
  // Tên = ngày + hash nội dung → thiết bị retry ghi đè đúng key, không sinh file rác.
  const key = `${DATA_PREFIX}${device}_${day}_${sha.slice(0, 12)}.json`;

  let fileOk = true;
  try {
    await env.R2.put(key, raw, { httpMetadata: { contentType: 'application/json; charset=utf-8' } });
  } catch (e) {
    console.error(`r2 put failed (${key}):`, e);
    fileOk = false;
  }

  let id = null;
  let dbOk = true;
  try {
    const t = payloadTime(data);
    id = await db.insertSession(env, data, t ? t.toISOString() : null);
  } catch (e) {
    console.error(`d1 insert failed (${key}):`, e);
    dbOk = false;
  }

  if (!fileOk && !dbOk) return fail(500, 'lưu thất bại cả R2 lẫn D1 — gửi lại');
  return json({ ok: true, file: key.slice(DATA_PREFIX.length), db: dbOk, id });
}
