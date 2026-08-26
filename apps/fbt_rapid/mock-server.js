/**
 * mock-server.js — chay ban WEB tren du lieu GIA de XEM giao dien, khong dung server that.
 *
 *   flutter build web --release --base-href /app/ --dart-define=FBT_URL=http://localhost:8090
 *   node mock-server.js            # mo http://localhost:8090/app/
 *
 * Dang nhap: BAT KY user/password nao cung vao duoc, vai tro root.
 *
 * Khac `serve-web-local.js` o mot diem duy nhat va do la ca diem: file kia PROXY ve server
 * that (can mang, can tai khoan, va du lieu that thi hiem khi co du trang thai de xem);
 * file nay TU BIA du lieu, chon sao cho MOI trang thai giao dien deu xuat hien it nhat mot
 * lan - may dung ban chung / may ghim rieng / may chua bao version / may khong ghim duoc /
 * lan do co loi cam bien / lich su cap nhat co ca moc that lan moc suy doan.
 *
 * KHONG dung file nay de deploy. No khong co xac thuc gi ca.
 */
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = Number(process.argv[2] || 8090);
const ROOT = path.resolve(__dirname, 'build', 'web');
const BASE = '/app';

const MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript',
  '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json',
  '.wasm': 'application/wasm', '.png': 'image/png', '.ico': 'image/x-icon',
  '.otf': 'font/otf', '.ttf': 'font/ttf', '.svg': 'image/svg+xml',
  '.bin': 'application/octet-stream', '.frag': 'application/octet-stream',
};

if (!fs.existsSync(path.join(ROOT, 'index.html'))) {
  console.error('Chua co build/web. Chay truoc:');
  console.error('  flutter build web --release --base-href /app/ --dart-define=FBT_URL=http://localhost:' + PORT);
  process.exit(1);
}
try {
  const js = fs.readFileSync(path.join(ROOT, 'main.dart.js'), 'utf8');
  if (!js.includes('http://localhost:' + PORT) && !js.includes('http://127.0.0.1:' + PORT)) {
    console.warn('CANH BAO: ban build khong tro ve http://localhost:' + PORT + ' -> se dinh CORS.');
    console.warn('  flutter build web --release --base-href /app/ --dart-define=FBT_URL=http://localhost:' + PORT);
    console.warn('');
  }
} catch (_) { /* chi la canh bao */ }

// --- Du lieu gia -----------------------------------------------------------
// Moc thoi gian tinh nguoc tu luc chay, de "lan gui cuoi" luon trong nhu that.
const now = Date.now();
const ago = (min) => new Date(now - min * 60000).toISOString();

const OTA_FILES = [
  { name: 'fbt_v2.4.6.bin', size: 2423408, modified: ago(60) },      // moi nhat
  { name: 'fbt_v2.4.5.bin', size: 2422672, modified: ago(60 * 26) }, // DANG CHON
  { name: 'fbt_v2.4.4.bin', size: 2422672, modified: ago(60 * 200) },// dang ghim rieng
  { name: 'fbt_v2.4.4_rc1.bin', size: 2410000 },                     // KHONG co ngay
];
const OTA_TARGET = 'fbt_v2.4.5.bin';
const OTA_PINS = {
  RPL03003: { file: 'fbt_v2.4.4.bin', by: 'nguyenvandat', at: ago(60 * 30) },
};

// Moi may mot trang thai KHAC nhau - do la ca diem cua bo du lieu nay.
const DEVICES = [
  // dung ban chung, da len dung ban  -> chip xanh "Chung" + "Dung ban"
  { id_device: 'RPL03001', sessions: 412, last_seen: ago(12), version: 'v2.4.5' },
  // ghim rieng, chua len              -> chip DO "Rieng" + "Cho nap"
  { id_device: 'RPL03003', sessions: 128, last_seen: ago(48), version: 'v2.4.5' },
  // dung ban chung, chua len          -> chip xanh + "Cho nap"
  { id_device: 'RPL02013', sessions: 136, last_seen: ago(60 * 5), version: 'v2.4.3' },
  // duoi v2.4.4 -> KHONG ghim rieng duoc: o chon thanh o TRO, vien nhat
  { id_device: 'RPL01001', sessions: 57, last_seen: ago(60 * 24 * 9), version: 'V2.3.1' },
  // ban co HAU TO: tinh la CHUA cap nhat du so trung
  { id_device: 'RPL02007', sessions: 125, last_seen: ago(60 * 24 * 3), version: 'v2.4.5AT' },
  // chua bao version bao gio          -> muc loc rieng "chua bao"
  { id_device: 'RPL09001', sessions: 3, last_seen: ago(60 * 24 * 40), version: '' },
];

const SLOT_NAMES = ['PC', 'EHP', 'EMS', 'WSSV', 'TPD', 'EHP', 'EMS', 'PC', 'WSSV', 'TPD'];
const VERDICTS = ['P', 'N', 'N', 'S', 'N', 'N', 'E', 'N', 'P', 'N'];

// Duong cong sigmoid gia + nhieu tat dan, du de nhin ra hinh dang tren chart.
function curve(slot, loops) {
  const positive = VERDICTS[slot] === 'P' || VERDICTS[slot] === 'S';
  const out = [];
  for (let i = 0; i < loops; i++) {
    const base = 820 + Math.sin((i + slot) / 7) * 4;
    const amp = positive ? 260 / (1 + Math.exp(-(i - 45 - slot * 2) / 6)) : 0;
    out.push(Math.round((base + amp) * 10) / 10);
  }
  return out;
}

const runsOf = (dev) => {
  const n = (DEVICES.find((d) => d.id_device === dev) || {}).sessions || 0;
  const total = Math.min(n, 34);
  return Array.from({ length: total }, (_, i) => ({
    id: 900000 + i,
    id_device: dev,
    received_at: ago(60 * 6 * (i + 1)),
    posted_at: ago(60 * 6 * (i + 1)),
    version: (DEVICES.find((d) => d.id_device === dev) || {}).version || '',
    type_upload: i % 3 === 0 ? 'Manual' : 'Auto',
    ct_value: VERDICTS.map((v, s) => (v === 'N' ? 0 : 18 + s * 1.7)),
    result: VERDICTS.map((v, s) => `${SLOT_NAMES[s]} | ${(18 + s * 1.7).toFixed(1)} | ${v}`),
  }));
};

const json = (res, body, code = 200) => {
  const s = JSON.stringify(body);
  res.writeHead(code, {
    'Content-Type': 'application/json; charset=utf-8',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': '*',
    'Access-Control-Allow-Methods': 'GET,POST,PUT,DELETE,OPTIONS',
    'Cache-Control': 'no-store',
  });
  res.end(s);
};

// --- API gia ---------------------------------------------------------------
function api(req, res, url) {
  const p = url.pathname;
  const q = url.searchParams;

  if (req.method === 'OPTIONS') return json(res, {});

  // Dang nhap: nhan tat ca, luon tra vai tro root de xem duoc moi thu.
  if (p === '/auth' && req.method === 'POST') {
    let body = '';
    req.on('data', (c) => (body += c));
    req.on('end', () => {
      let action = 'login';
      try { action = (JSON.parse(body) || {}).action || 'login'; } catch (_) {}
      if (action !== 'login') return json(res, { ok: false, error: 'mock: chi ho tro login' });
      json(res, {
        ok: true, username: 'mock', name: 'Tai khoan mock', role: 'root',
        ids: ['*'], active: true, email: '', apiToken: 'mock-token',
      });
    });
    return;
  }

  if (p === '/devices') return json(res, DEVICES);

  // Lich su cap nhat: co CA moc that (`how: update`) lan moc chi doi version.
  let m = p.match(/^\/devices\/([^/]+)\/fw-log$/);
  if (m) {
    if (m[1] !== 'RPL03003') return json(res, []);
    return json(res, [
      { version: 'v2.4.5', at: ago(60 * 30), how: 'update' }, // moc XAC NHAN
      { version: 'v2.4.4', at: ago(60 * 24 * 6) },            // suy tu doi version
    ]);
  }

  if (p === '/ota') {
    return json(res, { target: OTA_TARGET, devices: OTA_PINS, files: OTA_FILES });
  }
  if (p.startsWith('/ota/')) return json(res, { ok: true }); // chon/xoa: gia vo thanh cong

  if (p === '/sessions') {
    const dev = q.get('device');
    const all = dev ? runsOf(dev) : DEVICES.flatMap((d) => runsOf(d.id_device));
    const limit = Number(q.get('limit') || 10);
    const page = Number(q.get('page') || 1);
    return json(res, {
      total: all.length, page, limit,
      items: all.slice((page - 1) * limit, page * limit),
    });
  }

  // Loi cam bien - CHI lan do moi nhat cua RPL02007, de thay bang do xuat hien
  // dung mot cho chu khong phai o moi lan do.
  m = p.match(/^\/sessions\/(\d+)\/errors$/);
  if (m) {
    const id = Number(m[1]);
    if (id !== 900000) return json(res, { id, errors: [] });
    return json(res, {
      id,
      errors: [
        { at: ago(70), session_id: id + 1, slot: 'Slot 6', code: '1045',
          message: '[Sensor Light]- No data from sensor In Process Amplification 40 min' },
        { at: ago(70), session_id: id + 1, slot: 'Slot 7', code: '1046',
          message: '[Sensor Light]- No data from sensor In Process Amplification 40 min' },
        { at: ago(68), session_id: id + 1, slot: 'Slot 8', code: '1047',
          message: '[Sensor Light]- Sensor too dark In Process Lysis 10 min' },
      ],
    });
  }

  m = p.match(/^\/sessions\/(\d+)\/amplification$/);
  if (m) {
    const loops = 120;
    return json(res, {
      id: Number(m[1]),
      slots: VERDICTS.map((v, s) => ({
        slot: s, ct_value: v === 'N' ? 0 : 18 + s * 1.7,
        result: `${SLOT_NAMES[s]} | ${(18 + s * 1.7).toFixed(1)} | ${v}`,
        points: curve(s, loops),
      })),
    });
  }

  m = p.match(/^\/sessions\/(\d+)$/);
  if (m) {
    const id = Number(m[1]);
    return json(res, {
      id, id_device: 'RPL02007', received_at: ago(70), posted_at: ago(70),
      version: 'v2.4.5AT', type_upload: 'Auto', method: 'append',
      payload: {
        id_device: 'RPL02007', version: 'v2.4.5AT', kitId: '0.00',
        type_Upload: 'Auto', method: 'append',
        slopes: Array(10).fill(1), origins: Array(10).fill(0),
        LED_power: Array(10).fill(150),
        CT_value: VERDICTS.map((v, s) => (v === 'N' ? 0 : 18 + s * 1.7)),
        result: VERDICTS.map((v, s) => `${SLOT_NAMES[s]} | ${(18 + s * 1.7).toFixed(1)} | ${v}`),
      },
    });
  }

  if (p === '/' || p === '/whoami') return json(res, { ok: true, service: 'MOCK' });
  json(res, { ok: false, error: 'mock: chua lam route ' + p }, 404);
}

const serveStatic = (req, res) => {
  let rel = decodeURIComponent(req.url.split('?')[0]).slice(BASE.length) || '/';
  if (rel.endsWith('/')) rel += 'index.html';
  const file = path.join(ROOT, rel);
  if (!file.startsWith(ROOT) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    res.writeHead(404).end('not found');
    return;
  }
  res.writeHead(200, {
    'Content-Type': MIME[path.extname(file)] || 'application/octet-stream',
    'Cache-Control': 'no-store',
  });
  fs.createReadStream(file).pipe(res);
};

http.createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost:' + PORT);
  if (url.pathname === BASE || url.pathname.startsWith(BASE + '/')) {
    return serveStatic(req, res);
  }
  api(req, res, url);
}).listen(PORT, () => {
  console.log('MOCK dang chay - du lieu GIA, khong dung server that.');
  console.log('  Mo:        http://localhost:' + PORT + '/app/');
  console.log('  Dang nhap: bat ky user/password nao cung duoc (vai tro root).');
  console.log('');
  console.log('  Quan ly may -> Cap nhat OTA     : ban dang chon len dau, ban khong ngay xuong cuoi,');
  console.log('                                    nut xoa BI CHAN o ban dang chon va ban dang ghim.');
  console.log('  Quan ly may -> Trang thai may   : 6 may, moi may mot trang thai khac nhau;');
  console.log('                                    nut phieu trong o tim = loc theo firmware.');
  console.log('  Nut lich su (RPL03003)          : co ca moc THAT lan moc suy doan (dau ~).');
  console.log('  Lich su -> Engineer -> RPL02007 : mo lan do MOI NHAT de thay BANG MA LOI.');
});
