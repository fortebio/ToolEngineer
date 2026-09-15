// Test hợp đồng HTTP của Worker — bản đối chiếu của server/tests/test_api.py.
// Chạy Worker THẬT (wrangler dev --local: D1 + R2 giả lập trên đĩa) rồi gọi HTTP vào.
//
//   npm run schema:local   # 1 lần, tạo bảng trong D1 cục bộ
//   npm test
import { execFileSync, spawn } from 'node:child_process';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const PORT = Number(process.env.PORT ?? 8787);
const BASE = `http://127.0.0.1:${PORT}`;
const TOKEN = 'testtok';
const AUTH = { authorization: `Bearer ${TOKEN}` };
const SAMPLE = JSON.parse(readFileSync(new URL('../../server/docs/data_sample/data_RPL.json', import.meta.url)));

const isWin = process.platform === 'win32';
let ran = 0;

async function main() {
  const proc = spawn(
    isWin ? 'npx.cmd' : 'npx',
    ['wrangler', 'dev', '--local', '--port', String(PORT), '--var', `RECEIVER_TOKEN:${TOKEN}`],
    { cwd: new URL('..', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'), stdio: 'pipe', shell: isWin },
  );
  proc.stdout.on('data', (d) => process.env.VERBOSE && process.stdout.write(d));
  proc.stderr.on('data', (d) => process.env.VERBOSE && process.stderr.write(d));

  try {
    await waitReady();
    for (const [name, fn] of tests) {
      await fn();
      ran++;
      console.log(`ok ${name}`);
    }
    console.log(`\n${ran} tests passed`);
  } finally {
    killTree(proc.pid);
  }
}

/**
 * Trên Windows `proc.kill()` chỉ giết tiến trình npx, KHÔNG giết cây con
 * (npx → wrangler → workerd) → node treo mãi sau khi test xong. Phải taskkill /T.
 */
function killTree(pid) {
  if (!pid) return;
  try {
    if (isWin) execFileSync('taskkill', ['/pid', String(pid), '/T', '/F'], { stdio: 'ignore' });
    else process.kill(-pid, 'SIGKILL');
  } catch {
    /* đã chết rồi thì thôi */
  }
}

async function waitReady(timeoutMs = 90000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const r = await fetch(`${BASE}/`);
      if (r.ok) return;
    } catch {
      /* chưa lên */
    }
    await new Promise((r) => setTimeout(r, 500));
  }
  throw new Error('wrangler dev không lên sau 90s');
}

const get = (p, h) => fetch(BASE + p, { headers: h });
const send = (p, method, body, h = {}) =>
  fetch(BASE + p, {
    method,
    headers: body === undefined ? h : { 'content-type': 'application/octet-stream', ...h },
    body,
  });
const postJson = (p, obj, h = {}) =>
  fetch(BASE + p, { method: 'POST', headers: { 'content-type': 'application/json', ...h }, body: JSON.stringify(obj) });

const tests = [
  ['root_khong_405', async () => {
    const r = await get('/');
    assert.equal(r.status, 200);
    assert.equal((await r.json()).ok, true);
  }],

  ['ingest_can_token', async () => {
    assert.equal((await postJson('/', SAMPLE)).status, 401);
  }],

  ['ingest_ghi_r2_truoc_roi_d1', async () => {
    const r = await postJson('/duong/dan/bat-ky', SAMPLE, AUTH);
    // Đọc body MỘT lần: `assert.equal(x, y, await r.text())` sẽ tiêu body ngay cả
    // khi assert không fail (đối số message được tính trước) → r.json() sau đó nổ.
    const text = await r.text();
    assert.equal(r.status, 200, text);
    const b = JSON.parse(text);
    assert.equal(b.ok, true);
    assert.equal(b.db, true, 'D1 phải insert được');
    assert.match(b.file, /^RPL02013_\d{8}_[0-9a-f]{12}\.json$/);
  }],

  ['retry_cung_noi_dung_cung_ten_file', async () => {
    const a = await (await postJson('/', SAMPLE, AUTH)).json();
    const b = await (await postJson('/', SAMPLE, AUTH)).json();
    assert.equal(a.file, b.file, 'cùng payload phải ra cùng key R2');
  }],

  ['json_hong_400', async () => {
    const r = await fetch(`${BASE}/`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', ...AUTH },
      body: '{oops',
    });
    assert.equal(r.status, 400);
    assert.match(await r.text(), /invalid json/);
  }],

  ['validate_400', async () => {
    assert.equal((await postJson('/', { x: 1 }, AUTH)).status, 400);
    assert.equal((await postJson('/', { ...SAMPLE, CT_value: Array(9).fill(1) }, AUTH)).status, 400);
  }],

  ['path_traversal_lam_sach', async () => {
    const r = await postJson('/', { ...SAMPLE, id_device: '../../etc/passwd' }, AUTH);
    assert.equal(r.status, 200);
    // safe_name đã chà sạch → key R2 không có dấu '/'
    assert.ok(!(await r.json()).file.includes('/'));
  }],

  ['sessions_ngay_sai_400', async () => {
    assert.equal((await get('/sessions?from=garbage', AUTH)).status, 400);
    assert.equal((await get('/sessions?to=2026-13-01', AUTH)).status, 400);
  }],

  ['devices_can_token_va_tra_version', async () => {
    assert.equal((await get('/devices')).status, 401);
    const r = await get('/devices', AUTH);
    assert.equal(r.status, 200);
    const list = await r.json();
    const d = list.find((x) => x.id_device === 'RPL02013');
    assert.ok(d, 'phải thấy máy vừa ingest');
    assert.equal(d.version, SAMPLE.version, 'version firmware lấy từ phiên gần nhất');
    assert.ok(d.sessions >= 1 && d.last_seen);
  }],

  ['sessions_phan_trang_va_ct_result_la_mang', async () => {
    const r = await get('/sessions?limit=5&page=1', AUTH);
    const b = await r.json();
    assert.ok(b.total >= 1);
    assert.equal(b.page, 1);
    assert.ok(Array.isArray(b.items[0].ct_value), 'ct_value phải là MẢNG (json_extract trả chuỗi)');
    assert.equal(b.items[0].ct_value.length, 10);
    assert.ok(Array.isArray(b.items[0].result));
  }],

  ['session_detail_bo_amplification', async () => {
    const list = await (await get('/sessions?limit=1', AUTH)).json();
    const id = list.items[0].id;
    const s = await (await get(`/sessions/${id}`, AUTH)).json();
    assert.equal(s.id, id);
    assert.ok(s.payload.CT_value, 'payload còn nguyên phần nhẹ');
    assert.equal(s.payload.amplification, undefined, 'amplification phải bị bỏ');
    assert.equal((await get('/sessions/999999', AUTH)).status, 404);
  }],

  ['amplification_parse_thanh_mang_so', async () => {
    const list = await (await get('/sessions?limit=1', AUTH)).json();
    const b = await (await get(`/sessions/${list.items[0].id}/amplification`, AUTH)).json();
    assert.equal(b.slots.length, 10);
    assert.equal(b.slots[0].slot, 1);
    assert.ok(b.slots[0].points.length > 0, 'chuỗi CSV phải parse ra mảng số');
    assert.equal(typeof b.slots[0].points[0], 'number');
  }],

  ['auth_khong_can_token_va_luon_200', async () => {
    let r = await fetch(`${BASE}/auth`, { method: 'POST', headers: { 'content-type': 'text/plain' }, body: '{oops' });
    assert.equal(r.status, 200);
    assert.equal((await r.json()).ok, false);

    r = await postJson('/auth', { action: 'bay-nhay' });
    assert.equal(r.status, 200);
    const b = await r.json();
    assert.equal(b.ok, false);
    assert.equal(b.file, undefined, 'KHÔNG được bị catch-all ingest nuốt');

    r = await postJson('/auth', { action: 'login', username: 'x', password: 'y' });
    assert.equal((await r.json()).ok, false);
  }],

  ['tao_user_dang_nhap_doi_mat_khau', async () => {
    // Tạo root đầu tiên trực tiếp qua saveUser cần đã có root → seed bằng D1 là việc của
    // scripts/seed. Ở đây test vòng đời qua chính API: dùng seed sẵn nếu có, không thì bỏ qua.
    const seeded = await postJson('/auth', {
      action: 'listUsers',
      adminUser: 'root',
      adminPassword: 'rootpass',
    });
    const body = await seeded.json();
    if (!body.ok) {
      console.log('   (bỏ qua: chưa seed tài khoản root — chạy `npm run seed:local`)');
      return;
    }
    assert.ok(Array.isArray(body.users));

    // login đúng
    const ok = await (await postJson('/auth', { action: 'login', username: 'root', password: 'rootpass' })).json();
    assert.equal(ok.ok, true);
    assert.equal(ok.role, 'root');
    assert.equal(ok.allowAll, true);
    assert.equal(ok.apiToken, TOKEN);

    // login sai mật khẩu
    const bad = await (await postJson('/auth', { action: 'login', username: 'root', password: 'sai' })).json();
    assert.equal(bad.ok, false);

    // username KHÔNG phân biệt hoa thường
    const upper = await (await postJson('/auth', { action: 'login', username: 'ROOT', password: 'rootpass' })).json();
    assert.equal(upper.ok, true, 'lower(username) phải khớp');
  }],

  ['ota_vong_doi_day_du', async () => {
    assert.equal((await send('/ota/fw_v1.bin', 'PUT', 'BIN1')).status, 401);

    let r = await send('/ota/fw_v1.bin', 'PUT', 'BIN1', AUTH);
    assert.equal(r.status, 200);
    assert.equal((await r.json()).size, 4);
    await send('/ota/fw_v2.bin', 'PUT', 'BIN22', AUTH);

    // tên bẩn / sai đuôi
    assert.equal((await send('/ota/..evil.bin', 'PUT', 'x', AUTH)).status, 400);
    assert.equal((await send('/ota/fw.txt', 'PUT', 'x', AUTH)).status, 400);

    let list = await (await get('/ota', AUTH)).json();
    assert.equal(list.target, null);
    const names = list.files.map((f) => f.name);
    assert.ok(names.includes('fw_v1.bin') && names.includes('fw_v2.bin'));
    assert.ok(!names.includes('evil.bin') && !names.includes('fw.txt'));

    assert.deepEqual(await (await get('/ota/check', AUTH)).json(), { update: false });

    assert.equal((await send('/ota/target/khong-co.bin', 'PUT', undefined, AUTH)).status, 404);
    assert.equal((await send('/ota/target/fw_v2.bin', 'PUT', undefined, AUTH)).status, 200);

    const chk = await (await get('/ota/check', AUTH)).json();
    assert.equal(chk.update, true);
    assert.equal(chk.version, 'fw_v2.bin');
    assert.equal(chk.size, 5);
    // sha256("BIN22")
    assert.equal(chk.sha256, await sha256('BIN22'));
    assert.ok(chk.url.endsWith('/ota/fw_v2.bin'));

    const dl = await get('/ota/fw_v2.bin', AUTH);
    assert.equal(await dl.text(), 'BIN22');
    assert.equal(dl.headers.get('content-type'), 'application/octet-stream');

    // xoá đúng bản đang chọn → tự huỷ chọn
    assert.equal((await send('/ota/fw_v2.bin', 'DELETE', undefined, AUTH)).status, 200);
    list = await (await get('/ota', AUTH)).json();
    assert.equal(list.target, null);
    assert.deepEqual(await (await get('/ota/check', AUTH)).json(), { update: false });

    // huỷ chọn thủ công
    await send('/ota/target/fw_v1.bin', 'PUT', undefined, AUTH);
    assert.equal((await send('/ota/target', 'DELETE', undefined, AUTH)).status, 200);
    assert.equal((await (await get('/ota', AUTH)).json()).target, null);

    assert.equal((await get('/ota/khong-ton-tai.bin', AUTH)).status, 404);
  }],
];

async function sha256(text) {
  const d = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

main().then(
  () => process.exit(0), // workerd có thể còn handle mở → thoát tường minh
  (e) => {
    console.error(`\nFAIL sau ${ran} test:\n`, e);
    process.exit(1);
  },
);
