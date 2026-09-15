// Tạo 1 tài khoản root trong D1 CỤC BỘ để chạy test/dev. KHÔNG dùng cho production.
//   node scripts/seed_local.mjs [username] [password]
import { execFileSync } from 'node:child_process';
import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const [username = 'root', password = 'rootpass'] = process.argv.slice(2);
const ITER = 100000;

const hex = (buf) => [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');

const salt = crypto.getRandomValues(new Uint8Array(16));
const key = await crypto.subtle.importKey('raw', new TextEncoder().encode(password), 'PBKDF2', false, ['deriveBits']);
const bits = await crypto.subtle.deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt, iterations: ITER }, key, 256);
const stored = `pbkdf2$${ITER}$${hex(salt.buffer)}$${hex(bits)}`;

const sql =
  `DELETE FROM users WHERE lower(username) = lower('${username}');\n` +
  `INSERT INTO users (username, password, role, ids, name, active, email)\n` +
  `VALUES ('${username}', '${stored}', 'root', '["*"]', 'Root cuc bo', 1, '');\n`;

// Ghi ra FILE rồi --file: trên Windows `shell: true` sẽ chẻ chuỗi SQL theo khoảng
// trắng và nuốt dấu nháy nếu truyền qua --command.
const sqlPath = new URL('./_seed.generated.sql', import.meta.url);
writeFileSync(sqlPath, sql, 'utf8');

execFileSync(process.platform === 'win32' ? 'npx.cmd' : 'npx',
  ['wrangler', 'd1', 'execute', 'fbt', '--local', '--file', fileURLToPath(sqlPath)],
  { stdio: 'inherit', shell: process.platform === 'win32' });

console.log(`\nĐã seed ${username} / ${password} (role root, ids ["*"]) vào D1 cục bộ.`);
