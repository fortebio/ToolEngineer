// Tài khoản app — bản chuyển của server/app/auth.py, GIỮ NGUYÊN hợp đồng userAuth.js:
// POST /auth {action: login|changePassword|changeEmail|listUsers|saveUser|deleteUser}
// → LUÔN HTTP 200 {ok:true,...} / {ok:false,error} (app đọc cờ `ok`, không đọc status).
import * as db from './db.js';
import { hashPassword, isUnsupportedHash, parseActive, parseIds, verifyPassword } from './logic.js';

const LOGIN_FAIL_MSG = 'Sai tài khoản hoặc mật khẩu'; // dùng CHUNG mọi lỗi login (không lộ thông tin)
const NO_PERMISSION_MSG = 'Không có quyền';
const RESET_MSG =
  'Mật khẩu tài khoản này được băm bằng scrypt (server cũ) — Cloudflare Workers không chạy được scrypt. ' +
  'Nhờ root đặt lại mật khẩu.';
const ROLES = ['root', 'admin', 'user'];

export async function dispatch(env, body) {
  const handler = {
    login: doLogin,
    changePassword: changePassword,
    changeEmail: changeEmail,
    listUsers: listUsersAction,
    saveUser: saveUser,
    deleteUser: deleteUserAction,
  }[String(body.action ?? '').trim()];
  if (!handler) return { ok: false, error: 'Hành động không hợp lệ' };
  return handler(env, body);
}

/** Ép primitive về chuỗi; object/array → '' (khớp toPrimitiveStr_ userAuth.js). */
const str = (v) => (['string', 'number', 'boolean'].includes(typeof v) ? String(v) : '');

/** Xác thực 1 cặp user/pass. Trả {user} | {error}. */
async function checkLogin(env, username, password) {
  if (!username) return { error: LOGIN_FAIL_MSG };
  const u = await db.getUser(env, username);
  if (!u || !u.active) return { error: LOGIN_FAIL_MSG };
  // Phân biệt "sai mật khẩu" với "hash scrypt không đọc được" — nếu không user cứ
  // gõ đúng mật khẩu mà bị báo sai, không biết đường nào mà lần.
  if (isUnsupportedHash(u.password)) return { error: RESET_MSG };
  if (!(await verifyPassword(password, u.password))) return { error: LOGIN_FAIL_MSG };
  return { user: u };
}

async function doLogin(env, body) {
  const { user, error } = await checkLogin(env, str(body.username).trim(), str(body.password));
  if (!user) return { ok: false, error };
  const ids = user.ids ?? [];
  return {
    ok: true,
    username: user.username,
    name: user.name || '',
    email: user.email || '',
    role: user.role,
    ids,
    allowAll: user.role === 'root' || ids.includes('*'),
    // Token API cấp sau đăng nhập — app web không nhúng token vào JS được.
    // ⚠️ ĐÂY VẪN LÀ MASTER TOKEN, y như bản FastAPI: user hợp lệ nào cũng đọc
    // được toàn bộ API, lọc theo máy vẫn ở client. Chưa sửa trong bước chuyển
    // hạ tầng này (xem README mục "Nợ mang theo").
    apiToken: env.RECEIVER_TOKEN ?? '',
  };
}

async function changePassword(env, body) {
  const username = str(body.username).trim();
  const next = str(body.newPassword);
  if (!username || next === '') return { ok: false, error: 'Thiếu thông tin' };
  const { user, error } = await checkLogin(env, username, str(body.oldPassword));
  if (!user) return { ok: false, error };
  await db.setPassword(env, user.username, await hashPassword(next, iterOf(env)));
  return { ok: true, changed: user.username };
}

async function changeEmail(env, body) {
  const username = str(body.username).trim();
  if (!username) return { ok: false, error: 'Thiếu thông tin' };
  const { user, error } = await checkLogin(env, username, str(body.password));
  if (!user) return { ok: false, error };
  const email = str(body.email).trim();
  await db.setEmail(env, user.username, email);
  return { ok: true, email };
}

const iterOf = (env) => Number(env.PBKDF2_ITER ?? 100000) || 100000;

/** Gate admin: root + active + đúng mật khẩu (re-verify MỖI LẦN — app không lưu mật khẩu). */
async function requireAdmin(env, body) {
  const { user } = await checkLogin(env, str(body.adminUser).trim(), str(body.adminPassword));
  return user && user.role === 'root' ? user : null;
}

async function listUsersAction(env, body) {
  if (!(await requireAdmin(env, body))) return { ok: false, error: NO_PERMISSION_MSG };
  const users = (await db.listUsers(env)).map((u) => ({
    username: u.username,
    role: u.role,
    ids: u.ids ?? [],
    name: u.name || '',
    email: u.email || '',
    active: u.active,
  }));
  return { ok: true, users };
}

async function saveUser(env, body) {
  if (!(await requireAdmin(env, body))) return { ok: false, error: NO_PERMISSION_MSG };
  const u = body.user;
  if (!u || typeof u !== 'object') return { ok: false, error: 'Thiếu thông tin user' };
  const username = str(u.username).trim();
  if (!username) return { ok: false, error: 'Thiếu username' };
  const role = str(u.role).trim().toLowerCase() || 'user';
  if (!ROLES.includes(role)) return { ok: false, error: 'role không hợp lệ (chỉ root/admin/user)' };

  const ids = parseIds(u.ids);
  const name = str(u.name).trim();
  const active = parseActive(u.active);
  const pw = str(u.password);
  const email = str(u.email).trim();

  // CHẶN hạ quyền/khoá root hoạt động CUỐI CÙNG (chống tự khoá mình ra ngoài).
  const existing = await db.getUser(env, username);
  if (
    existing &&
    existing.role === 'root' &&
    existing.active &&
    (role !== 'root' || !active) &&
    (await db.countRoots(env, true)) <= 1
  ) {
    return { ok: false, error: 'Không thể hạ quyền/khoá root cuối cùng' };
  }

  const created = await db.upsertUser(env, {
    username,
    role,
    ids,
    name,
    active,
    passwordHash: pw ? await hashPassword(pw, iterOf(env)) : null,
    email: email || null,
  });
  return { ok: true, saved: username, created };
}

async function deleteUserAction(env, body) {
  if (!(await requireAdmin(env, body))) return { ok: false, error: NO_PERMISSION_MSG };
  const username = str(body.username).trim();
  if (!username) return { ok: false, error: 'Thiếu username' };
  const target = await db.getUser(env, username);
  if (!target) return { ok: false, error: `Không tìm thấy user: ${username}` };
  if (target.role === 'root' && (await db.countRoots(env)) <= 1) {
    return { ok: false, error: 'Không thể xoá root cuối cùng' };
  }
  await db.deleteUser(env, username);
  return { ok: true, deleted: username };
}
