// Chuyển JSON RAW của firmware (key kiểu CT_value/amplification/result "-- | N")
// sang "app shape" mà Flutter `TestResult.fromCloudRun` đọc (key lowercase).

function asArray(x) {
  return Array.isArray(x) ? x : [];
}

// Số hợp lệ hoặc null ("--"/"N/A"/rỗng → null).
function num(x) {
  if (x === null || x === undefined) return null;
  if (typeof x === 'number') return Number.isFinite(x) ? x : null;
  const s = String(x).trim();
  if (s === '' || s === '--' || s.toUpperCase() === 'N/A') return null;
  const v = Number(s);
  return Number.isFinite(v) ? v : null;
}

// result firmware dạng "03.0 | N" / "-- | N" → chữ phân loại "N/P/S/E/?".
function resultLetter(x) {
  const s = String(x ?? '').trim();
  const part = s.includes('|') ? s.split('|').pop() : s;
  const c = part.trim().charAt(0).toUpperCase();
  return c || '?';
}

// "dd-MM-yyyy HH:mm:ss" (giờ VN, coi UTC+7) hoặc ISO → Date.
function parseRunTime(s) {
  if (!s) return new Date(NaN);
  const str = String(s).trim();
  const m = str.match(/^(\d{2})-(\d{2})-(\d{4})[ T](\d{2}):(\d{2}):(\d{2})$/);
  if (m) {
    const [, dd, MM, yyyy, hh, mi, ss] = m;
    return new Date(`${yyyy}-${MM}-${dd}T${hh}:${mi}:${ss}+07:00`);
  }
  return new Date(str);
}

function isoOf(t) {
  const d = t instanceof Date ? t : new Date(t);
  return Number.isNaN(d.getTime()) ? null : d.toISOString();
}

// row = {id, device_id, version, run_time, raw}. withCurves=true → kèm `curves`.
// `curves` để NGUYÊN mảng chuỗi "a,b,c,…" (raw amplification) — app tự tách
// chuỗi comma-separated trong _parseRawCurve, nên không cần parse ở server.
function toAppRun(row, withCurves) {
  const raw = row.raw || {};
  const run = {
    fileId: String(row.id),
    id_device: raw.id_device ?? row.device_id ?? '',
    version: raw.version ?? row.version ?? '',
    // Giữ chuỗi thời gian gốc — app fromCloudRun nhận cả "dd-MM-yyyy HH:mm:ss" lẫn ISO.
    time: raw.time ?? isoOf(row.run_time),
    result: asArray(raw.result).map(resultLetter),
    ct: asArray(raw.CT_value).map(num),
    slopes: asArray(raw.slopes).map(num),
  };
  if (withCurves) run.curves = asArray(raw.amplification);
  return run;
}

module.exports = { parseRunTime, isoOf, toAppRun, resultLetter, num };
