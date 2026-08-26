// Host bản web Flutter NGAY TRÊN MÁY NÀY, có proxy API — chạy được đầy đủ,
// đăng nhập được.
//
//   node serve-web-local.js            # cổng 8080, proxy về hub.fortebio.tech
//   node serve-web-local.js 9000       # đổi cổng
//   node serve-web-local.js 8080 https://fbt.basa-luma.ts.net   # đổi server gốc
//
// PHẢI build kèm cờ URL, nếu không app vẫn gọi thẳng hub.fortebio.tech và trình
// duyệt chặn CORS (khác origin):
//
//   flutter build web --release --base-href /app/ --dart-define=FBT_URL=http://localhost:8080
//
// Vì sao cần proxy: app gọi API bằng URL TUYỆT ĐỐI. Trang chạy ở localhost mà
// gọi sang hub.fortebio.tech là hai origin khác nhau -> trình duyệt chặn, và
// server không gửi `Access-Control-Allow-Origin`. Cho cùng một origin phục vụ CẢ
// trang LẪN API thì không còn khái niệm "khác origin" nữa.
//
// Chỉ dùng để chạy/kiểm thử tại chỗ. Nó KHÔNG thay server thật: dữ liệu vẫn đi
// về `hub.fortebio.tech`, chỉ khác đường đi.

const http = require('http');
const https = require('https');
const fs = require('fs');
const path = require('path');

const PORT   = Number(process.argv[2] || 8080);
const ORIGIN = (process.argv[3] || 'https://hub.fortebio.tech').replace(/\/+$/, '');
const ROOT   = path.resolve(__dirname, 'build', 'web');
const BASE   = '/app';

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

// Canh bao neu ban build khong tro ve chinh server nay -> se dinh CORS.
// Doc DUNG `main.dart.js` - file chuan `flutter build web` vua sinh. Truoc do
// cho quet theo mau `main.*.dart.js` thi no trung ban VAN TAY con sot lai cua lan
// deploy truoc (sap xep '5' dung truoc 'd') va bao nham la build sai co.
try {
  const mainJs = path.join(ROOT, 'main.dart.js');
  if (fs.existsSync(mainJs)) {
    const js = fs.readFileSync(mainJs, 'utf8');
    if (!js.includes('http://localhost:' + PORT) && !js.includes('http://127.0.0.1:' + PORT)) {
      console.warn('CANH BAO: ban build khong tro ve http://localhost:' + PORT + '.');
      console.warn('  Dang nhap se bi CORS chan. Build lai voi:');
      console.warn('  flutter build web --release --base-href /app/ --dart-define=FBT_URL=http://localhost:' + PORT);
      console.warn('');
    }
  }
} catch (_) { /* chi la canh bao */ }

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
    // Khong cache o may minh: dang sua giao dien thi F5 phai thay ngay.
    'Cache-Control': 'no-store',
  });
  fs.createReadStream(file).pipe(res);
};

const proxy = (req, res) => {
  const target = new URL(ORIGIN + req.url);
  const headers = { ...req.headers, host: target.host };
  delete headers['accept-encoding']; // khoi phai giai nen de chuyen tiep
  const up = https.request(
    { hostname: target.hostname, port: 443, path: target.pathname + target.search,
      method: req.method, headers },
    (r) => { res.writeHead(r.statusCode, r.headers); r.pipe(res); }
  );
  up.on('error', (e) => {
    console.error('  proxy loi:', req.method, req.url, e.message);
    res.writeHead(502).end('proxy error: ' + e.message);
  });
  req.pipe(up);
};

http.createServer((req, res) => {
  const p = req.url.split('?')[0];
  if (p === '/' ) { res.writeHead(302, { Location: BASE + '/' }).end(); return; }
  if (p === BASE || p.startsWith(BASE + '/')) return serveStatic(req, res);
  return proxy(req, res);   // moi thu con lai -> server that
}).listen(PORT, () => {
  console.log('Web local  : http://localhost:' + PORT + BASE + '/');
  console.log('API proxy  : * -> ' + ORIGIN);
  console.log('Thu muc    : ' + ROOT);
  console.log('Dung: Ctrl+C');
});
