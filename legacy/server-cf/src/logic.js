// Hàm thuần — bản chuyển của server/app/logic.py. Không đụng D1/R2 nên test độc lập được.

const VN_OFFSET_MIN = 7 * 60;
// Thời điểm đo nằm trong tên file hoặc trường 'time'. Khớp cả ':' (gốc Drive) và '_' (tải trên Windows).
const ISO_RE = /(\d{4})-(\d{2})-(\d{2})T(\d{2})[_:](\d{2})[_:](\d{2})(?:\.\d+)?Z/;
const ALT_RE = /(\d{2})-(\d{2})-(\d{4})[ _](\d{2})[_:](\d{2})[_:](\d{2})/;

/** Chuỗi ISO UTC dùng thống nhất cho mọi cột thời gian trong D1 (so sánh chuỗi = so sánh thời gian). */
export const isoUtc = (d = new Date()) => d.toISOString();

/** Đọc thời điểm đo từ chuỗi; null nếu không khớp / ngày giờ vô lý. */
export function parseTs(name) {
  const s = String(name || '');
  let m = ISO_RE.exec(s);
  if (m) {
    const [, y, mo, d, h, mi, sec] = m.map(Number);
    return validDate(Date.UTC(y, mo - 1, d, h, mi, sec), y, mo, d, h, mi, sec);
  }
  m = ALT_RE.exec(s);
  if (m) {
    const [, d, mo, y, h, mi, sec] = m.map(Number);
    // DD-MM-YYYY không có Z → giờ VN, quy về UTC.
    return validDate(Date.UTC(y, mo - 1, d, h, mi, sec) - VN_OFFSET_MIN * 60000, y, mo, d, h, mi, sec);
  }
  return null;
}

/** Chặn "tháng 45 / giờ 25": JS tự tràn sang tháng sau thay vì báo lỗi như Python. */
function validDate(ms, y, mo, d, h, mi, s) {
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) return null;
  const dt = new Date(ms);
  return Number.isNaN(dt.getTime()) ? null : dt;
}

/** Thời điểm đo từ trường 'time' của payload; null nếu thiếu/không parse được. */
export function payloadTime(data) {
  if (!data || typeof data !== 'object') return null;
  return parseTs(String(data.time ?? ''));
}

/** Chỉ giữ ký tự an toàn cho tên file — chống path traversal. Giống hệt safe_name của Python. */
export function safeName(s) {
  const cleaned = String(s).replace(/[^A-Za-z0-9_.-]/g, '_');
  return stripEnds(cleaned).slice(0, 64) || 'unknown';
}

/** Python .strip('._') — bỏ mọi '.' và '_' ở HAI ĐẦU. */
function stripEnds(s) {
  let a = 0;
  let b = s.length;
  while (a < b && (s[a] === '.' || s[a] === '_')) a++;
  while (b > a && (s[b - 1] === '.' || s[b - 1] === '_')) b--;
  return s.slice(a, b);
}

/** So sánh chuỗi hằng thời gian (WebCrypto không có sẵn hàm này). */
export function timingSafeEqual(a, b) {
  const ba = new TextEncoder().encode(a);
  const bb = new TextEncoder().encode(b);
  // Độ dài khác nhau vẫn phải quét hết để không lộ độ dài qua thời gian.
  let diff = ba.length ^ bb.length;
  const n = Math.max(ba.length, bb.length);
  for (let i = 0; i < n; i++) diff |= (ba[i] ?? 0) ^ (bb[i] ?? 0);
  return diff === 0;
}

/** fail-CLOSED: chưa đặt token → TỪ CHỐI hết (giữ đúng hành vi check_auth của Python). */
export function checkAuth(authHeader, token) {
  if (!token) return false;
  return timingSafeEqual(String(authHeader || ''), `Bearer ${token}`);
}

const ARRAY_FIELDS = ['CT_value', 'result', 'record_out', 'amplification'];

/** Chặn rác hiển nhiên; trả thông báo lỗi hoặc null. */
export function validate(data) {
  if (!data || typeof data !== 'object' || Array.isArray(data)) return 'missing id_device';
  if (!String(data.id_device ?? '').trim()) return 'missing id_device';
  for (const f of ARRAY_FIELDS) {
    if (f in data && !(Array.isArray(data[f]) && data[f].length === 10)) {
      return `${f} must be a list of 10 items`;
    }
  }
  return null;
}

/** JSON chuẩn hoá (khoá sắp xếp, không khoảng trắng) để băm nội dung. */
export function canonicalJson(value) {
  if (value === null || typeof value !== 'object') return JSON.stringify(value);
  if (Array.isArray(value)) return `[${value.map(canonicalJson).join(',')}]`;
  const keys = Object.keys(value).sort();
  return `{${keys.map((k) => `${JSON.stringify(k)}:${canonicalJson(value[k])}`).join(',')}}`;
}

const hex = (buf) => [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');

export async function sha256Hex(text) {
  return hex(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text)));
}

/**
 * Băm nội dung phiên đo.
 * ⚠️ KHÔNG khớp với canonical_sha256 của Python khi payload có số THỰC:
 * Python json.dumps ra "1.0", JS JSON.stringify ra "1". Chỉ ảnh hưởng dedup CHÉO
 * giữa 2 backend (bảng sessions vốn không unique theo hash); trong phạm vi Worker
 * thì cùng payload vẫn ra cùng hash → thiết bị retry không sinh bản ghi/file rác.
 */
export const canonicalSha256Hex = (data) => sha256Hex(canonicalJson(data));

/** Chuẩn hoá danh sách mã máy (khớp parse_ids của Python / parseIds_ userAuth.js). */
export function parseIds(raw) {
  const parts = Array.isArray(raw) ? raw : String(raw ?? '').split(/[\s,;]+/);
  const out = [];
  const seen = new Set();
  for (const p of parts) {
    const t = String(p).trim();
    if (t && !seen.has(t)) {
      seen.add(t);
      out.push(t);
    }
  }
  return out;
}

/** TRUE/FALSE robust — trống/lạ = active (khớp parse_active). */
export function parseActive(v) {
  if (typeof v === 'boolean') return v;
  return !['false', '0', 'no', 'n'].includes(String(v ?? '').trim().toLowerCase());
}

/** 10 chuỗi CSV '150,153,...' → mảng số sẵn cho fl_chart. */
export function parseAmplification(payload) {
  const ct = payload?.CT_value ?? [];
  const res = payload?.result ?? [];
  const amp = payload?.amplification ?? [];
  return amp.map((csv, i) => ({
    slot: i + 1,
    ct_value: i < ct.length ? ct[i] : null,
    result: i < res.length ? res[i] : null,
    points: String(csv)
      .split(',')
      .map((x) => x.trim())
      .filter(Boolean)
      .map(Number)
      .filter((n) => !Number.isNaN(n)), // token không phải số → bỏ, không làm hỏng cả endpoint
  }));
}

// --- Mật khẩu --------------------------------------------------------------
// Workers KHÔNG có scrypt (hashlib.scrypt của Python không có bản WebCrypto).
// → Định dạng mới là PBKDF2-SHA256; vẫn ĐỌC được 'sha256$salt$hash' của Google
// Sheet cũ để tài khoản di cư từ Sheet đăng nhập được ngay.
// Tài khoản đang ở dạng 'scrypt$...' (tạo/đổi mật khẩu trên server FastAPI) sẽ
// KHÔNG đăng nhập được → phải đặt lại mật khẩu. Xem README mục "Di cư".

export const isUnsupportedHash = (stored) => String(stored || '').startsWith('scrypt$');

async function pbkdf2Hex(password, saltBytes, iterations) {
  const key = await crypto.subtle.importKey('raw', new TextEncoder().encode(password), 'PBKDF2', false, [
    'deriveBits',
  ]);
  const bits = await crypto.subtle.deriveBits(
    { name: 'PBKDF2', hash: 'SHA-256', salt: saltBytes, iterations },
    key,
    256,
  );
  return hex(bits);
}

const bytesFromHex = (h) => new Uint8Array(h.match(/../g)?.map((b) => parseInt(b, 16)) ?? []);

export async function hashPassword(password, iterations = 100000) {
  const salt = crypto.getRandomValues(new Uint8Array(16));
  const h = await pbkdf2Hex(password, salt, iterations);
  return `pbkdf2$${iterations}$${hex(salt.buffer)}$${h}`;
}

export async function verifyPassword(password, stored) {
  const parts = String(stored || '').split('$');
  try {
    if (parts[0] === 'pbkdf2' && parts.length === 4) {
      const [, iter, saltHex, hashHex] = parts;
      return timingSafeEqual(await pbkdf2Hex(password, bytesFromHex(saltHex), Number(iter)), hashHex);
    }
    if (parts[0] === 'sha256' && parts.length === 3) {
      // legacy Sheet: salt NỐI CHUỖI với mật khẩu rồi sha256 (không phải salt bytes)
      const [, salt, hashHex] = parts;
      return timingSafeEqual(await sha256Hex(salt + password), hashHex);
    }
  } catch {
    return false;
  }
  return false; // scrypt$ và định dạng lạ
}
