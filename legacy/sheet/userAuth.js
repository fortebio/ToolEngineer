/* =====================================================================
 *  FBT_RAPID — ACCOUNTS / AUTH WEB APP  (Google Apps Script, standalone)
 *  ---------------------------------------------------------------------
 *  Đây là một project Apps Script RIÊNG (tách khỏi getData.js). Nó có
 *  deployment riêng, /exec URL riêng, và Google Sheet RIÊNG.
 *
 *  Quản lý tài khoản đăng nhập cho app desktop FBT_RAPID: lưu user, vai
 *  trò (admin/user), danh sách mã máy (device codes) mỗi user được phép
 *  dùng, và xác thực đăng nhập.
 *
 *  ----------------------------- SETUP -------------------------------
 *  1) Tạo MỘT Google Sheet mới (riêng cho accounts, KHÔNG dùng chung
 *     sheet của getData.js).
 *  2) Trong sheet đó tạo một tab tên đúng là "Accounts".
 *  3) Dán HÀNG TIÊU ĐỀ (header) vào hàng 1 của tab "Accounts":
 *
 *        username | password | role | ids | name | active
 *
 *  4) Lấy ID của sheet (phần giữa /d/ và /edit trong URL) rồi điền vào
 *     hằng số ACCOUNTS_SHEET_ID bên dưới.
 *  5) Deploy → New deployment → type = Web app:
 *        - Execute as       = Me (chủ sở hữu sheet)
 *        - Who has access   = Anyone
 *     Copy URL dạng .../exec rồi dán vào app Flutter (endpoint accounts).
 *  6) Khi sửa code phải Deploy lại (Manage deployments → Edit → New
 *     version) thì /exec mới cập nhật.
 *
 *  -------------------------- SCHEMA ---------------------------------
 *  Tab "Accounts" — hàng 1 là header, mỗi hàng sau là 1 user:
 *
 *   | username | password | role  | ids               | name         | active |
 *   |----------|----------|-------|-------------------|--------------|--------|
 *   | admin    | 123456   | admin | *                 | Quản trị     | TRUE   |
 *   | lab01    | abc123   | user  | RPL02007,RPL02100 | Nguyễn Văn A | TRUE   |
 *
 *   - role  ∈ {"admin","user"}.
 *   - ids   = các mã máy ngăn cách bởi dấu phẩy / chấm phẩy / khoảng
 *             trắng. admin có thể dùng "*" nghĩa là TẤT CẢ máy.
 *   - active= TRUE/FALSE (để trống = đang hoạt động). FALSE = khoá.
 *             Có thể dùng ô checkbox (boolean) cho cột này — code ghi
 *             lại bằng boolean thật nên checkbox được giữ nguyên.
 *   Cột được dò theo TÊN header (không phân biệt hoa thường); nếu không
 *   thấy header sẽ fallback theo thứ tự cố định A..F như trên.
 *
 *   LƯU Ý về "active" trả về cho client: listUsers luôn trả "active" là
 *   boolean ĐÃ CHUẨN HOÁ (parseActive_): ô trống / giá trị lạ → true
 *   (đang hoạt động); chỉ FALSE/0/no/n → false. Client KHÔNG phân biệt
 *   được "ô trống mặc định" với "đặt rõ TRUE"; nếu cần thì thêm cờ riêng.
 *
 *   GHI CHÚ về tạo user mới (appendRow): user mới được thêm bằng
 *   appendRow → ghi vào hàng vật lý cuối cùng của sheet (getLastRow).
 *   Nếu tab có các hàng trống ở cuối (do xoá tay), user mới sẽ nằm SAU
 *   các hàng trống đó. readAccounts_ bỏ qua hàng trống nên việc đọc vẫn
 *   đúng; chỉ là layout có thể có khoảng trống. Nên giữ vùng dữ liệu liền
 *   mạch (xoá hẳn hàng thay vì chỉ xoá nội dung) để gọn gàng.
 *
 *  --------------------------- API -----------------------------------
 *  doGet  : health-check → {ok:true, service:"accounts"}.
 *           (Hỗ trợ JSONP qua ?callback= cho health-check, callback được
 *            kiểm tra theo charset an toàn; không hợp lệ thì trả JSON.)
 *  doPost : body JSON, dispatch theo body.action (POST để mật khẩu KHÔNG
 *           nằm trên URL). doPost LUÔN trả application/json (KHÔNG hỗ trợ
 *           JSONP — endpoint POST không thể nạp qua <script src> nên
 *           JSONP chỉ là bề mặt tấn công). Actions:
 *     - login      {username, password}
 *     - listUsers  {adminUser, adminPassword}                (admin-gated)
 *     - saveUser   {adminUser, adminPassword, user:{...}}    (admin-gated)
 *     - deleteUser {adminUser, adminPassword, username}      (admin-gated)
 *   Thành công: {ok:true, ...}. Thất bại: {ok:false, error:"..."}.
 *
 *  ------------------------ SECURITY NOTE ----------------------------
 *  Mật khẩu lưu dạng BĂM CÓ SALT: "sha256$<salt>$<hash>" (xem verifyPassword_
 *  / makePasswordHash_). Tài khoản plaintext CŨ vẫn đăng nhập được (tương
 *  thích ngược); đổi mật khẩu / tạo user mới sẽ tự băm. Chạy
 *  migratePasswordsToHash() MỘT lần để băm hết plaintext còn lại → sheet
 *  không còn mật khẩu thật. Client KHÔNG bao giờ nhận mật khẩu user khác.
 *  Sheet vẫn nên để RIÊNG TƯ; deploy "Who has access = Anyone".
 * ===================================================================== */

var ACCOUNTS_SHEET_ID = "1cK-uAlUSHCdqt8WGdr6kxBzhqoUCi0IRioh7IcdGKWw"; // <-- điền ID Google Sheet accounts
var ACCOUNTS_TAB = "Accounts";
var LOGIN_FAIL_MSG = "Sai tài khoản hoặc mật khẩu"; // dùng CHUNG cho mọi lỗi login
var NO_PERMISSION_MSG = "Không có quyền";
var MAX_POST_BYTES = 100000; // chặn payload quá lớn (~100KB) gây tốn quota
var CALLBACK_RE = /^[A-Za-z_$][A-Za-z0-9_$.]{0,64}$/; // JSONP callback an toàn

/* ------------------------- HTTP entrypoints ------------------------ */

function doGet(e) {
  var p = (e && e.parameter) || {};
  var callback = safeCallback_(p.callback);
  try {
    return reply_({ ok: true, service: "accounts" }, callback);
  } catch (err) {
    return reply_(
      { ok: false, error: String((err && err.message) || err) },
      callback,
    );
  }
}

function doPost(e) {
  // doPost LUÔN trả JSON (không JSONP) — xem ghi chú bảo mật ở đầu file.
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return reply_({ ok: false, error: "Thiếu dữ liệu POST" }, null);
    }
    var contents = e.postData.contents;
    if (String(contents).length > MAX_POST_BYTES) {
      return reply_({ ok: false, error: "Dữ liệu POST quá lớn" }, null);
    }

    var body;
    try {
      body = JSON.parse(contents);
    } catch (parseErr) {
      return reply_({ ok: false, error: "JSON không hợp lệ" }, null);
    }
    if (!body || typeof body !== "object" || isArray_(body)) {
      return reply_({ ok: false, error: "Body không hợp lệ" }, null);
    }

    var action = String((body && body.action) || "").trim();
    var out;
    switch (action) {
      case "login":
        out = actionLogin_(body);
        break;
      case "listUsers":
        out = actionListUsers_(body);
        break;
      case "saveUser":
        out = actionSaveUser_(body);
        break;
      case "deleteUser":
        out = actionDeleteUser_(body);
        break;
      case "changePassword":
        out = actionChangePassword_(body);
        break;
      case "changeEmail":
        out = actionChangeEmail_(body);
        break;
      default:
        out = { ok: false, error: "Hành động không hợp lệ" };
    }
    return reply_(out, null);
  } catch (err) {
    return reply_(
      { ok: false, error: String((err && err.message) || err) },
      null,
    );
  }
}

/* JSON / JSONP response helper (giống style getData.js).
 * callback PHẢI đã được kiểm tra an toàn (safeCallback_) trước khi truyền;
 * nếu null/rỗng thì trả application/json. */
function reply_(obj, callback) {
  var json = JSON.stringify(obj);
  if (callback) {
    return ContentService.createTextOutput(
      callback + "(" + json + ")",
    ).setMimeType(ContentService.MimeType.JAVASCRIPT);
  }
  return ContentService.createTextOutput(json).setMimeType(
    ContentService.MimeType.JSON,
  );
}

/* Chỉ chấp nhận tên callback JSONP theo charset an toàn; ngược lại null
 * (sẽ trả JSON) để tránh phản chiếu input tuỳ ý vào response JS. */
function safeCallback_(cb) {
  if (cb == null) return null;
  var s = String(cb);
  return CALLBACK_RE.test(s) ? s : null;
}

/* --------------------------- Actions ------------------------------- */

function actionLogin_(body) {
  var username = trim_(toPrimitiveStr_(body && body.username));
  var password = toPrimitiveStr_(body && body.password);
  if (!username) return { ok: false, error: LOGIN_FAIL_MSG };

  var model = readAccounts_();
  var rec = findUser_(model, username);

  // Phản hồi GIỐNG NHAU cho: không có user / sai mật khẩu / bị khoá.
  // Đánh giá MỌI điều kiện thành boolean trước (tránh short-circuit lộ
  // thông tin qua timing) và so khớp mật khẩu kiểu constant-time.
  var notFound = !rec;
  var inactive = rec ? !rec.active : true;
  var badPass = rec
    ? !verifyPassword_(rec.password, password)
    : true;
  if (notFound || inactive || badPass) {
    return { ok: false, error: LOGIN_FAIL_MSG };
  }

  var ids = parseIds_(rec.ids);
  // Full view CHỈ cho root (super-admin) hoặc ids chứa "*". Admin (nhân viên)
  // KHÔNG còn auto thấy mọi máy → chỉ thấy đúng mã được cấp.
  var allowAll = rec.role === "root" || ids.indexOf("*") >= 0;
  return {
    ok: true,
    username: rec.username,
    name: rec.name,
    email: rec.email,
    role: rec.role,
    ids: ids,
    allowAll: allowAll,
  };
}

/* User TỰ đổi mật khẩu: xác thực bằng mật khẩu hiện tại (oldPassword). */
function actionChangePassword_(body) {
  var lock = LockService.getScriptLock();
  if (!tryLock_(lock)) {
    return { ok: false, error: "Hệ thống đang bận, vui lòng thử lại" };
  }
  try {
    var username = trim_(toPrimitiveStr_(body && body.username));
    var oldPassword = toPrimitiveStr_(body && body.oldPassword);
    var newPassword = toPrimitiveStr_(body && body.newPassword);
    if (!username || newPassword === "") {
      return { ok: false, error: "Thiếu thông tin" };
    }
    var model = readAccounts_();
    var rec = findUser_(model, username);
    // Sai user / khoá / sai mật khẩu hiện tại → lỗi chung (không lộ).
    if (
      !rec ||
      !rec.active ||
      !verifyPassword_(rec.password, oldPassword)
    ) {
      return { ok: false, error: LOGIN_FAIL_MSG };
    }
    model.sheet
      .getRange(rec.rowIndex, model.col.password + 1)
      .setValue(makePasswordHash_(newPassword));
    return { ok: true, changed: rec.username };
  } finally {
    lock.releaseLock();
  }
}

/* User TỰ đổi email: xác thực bằng mật khẩu hiện tại. Cần sheet có cột email. */
function actionChangeEmail_(body) {
  var lock = LockService.getScriptLock();
  if (!tryLock_(lock)) {
    return { ok: false, error: "Hệ thống đang bận, vui lòng thử lại" };
  }
  try {
    var username = trim_(toPrimitiveStr_(body && body.username));
    var password = toPrimitiveStr_(body && body.password);
    var email = trim_(toPrimitiveStr_(body && body.email));
    if (!username) return { ok: false, error: "Thiếu thông tin" };
    var model = readAccounts_();
    var rec = findUser_(model, username);
    if (
      !rec ||
      !rec.active ||
      !verifyPassword_(rec.password, password)
    ) {
      return { ok: false, error: LOGIN_FAIL_MSG };
    }
    if (model.col.email < 0) {
      return {
        ok: false,
        error: 'Sheet "Accounts" chưa có cột "email" — thêm vào hàng tiêu đề.',
      };
    }
    model.sheet.getRange(rec.rowIndex, model.col.email + 1).setValue(email);
    return { ok: true, email: email };
  } finally {
    lock.releaseLock();
  }
}

function actionListUsers_(body) {
  var gate = requireAdmin_(body);
  if (!gate.ok) return gate;

  var model = gate.model;
  var users = model.rows.map(function (rec) {
    return {
      username: rec.username,
      role: rec.role,
      ids: parseIds_(rec.ids),
      name: rec.name,
      email: rec.email,
      active: rec.active, // boolean đã chuẩn hoá; KHÔNG bao giờ trả password
    };
  });
  return { ok: true, users: users };
}

function actionSaveUser_(body) {
  var lock = LockService.getScriptLock();
  if (!tryLock_(lock)) {
    return { ok: false, error: "Hệ thống đang bận, vui lòng thử lại" };
  }
  try {
    // Admin gate CHẠY TRƯỚC mọi validation field → caller chưa xác thực
    // luôn nhận NO_PERMISSION_MSG, không lộ thông điệp validation.
    var gate = requireAdmin_(body);
    if (!gate.ok) return gate;

    var u = (body && body.user) || {};
    if (typeof u !== "object" || isArray_(u)) {
      return { ok: false, error: "Thiếu thông tin user" };
    }

    var username = trim_(toPrimitiveStr_(u.username));
    if (!username) return { ok: false, error: "Thiếu username" };

    var role = trim_(toPrimitiveStr_(u.role)).toLowerCase();
    if (!role) role = "user";
    if (role !== "admin" && role !== "user" && role !== "root") {
      return { ok: false, error: "role không hợp lệ (chỉ root/admin/user)" };
    }

    var model = gate.model;
    var sheet = model.sheet;
    var col = model.col;
    var existing = findUser_(model, username);

    var idsStr = normalizeIdsToString_(u.ids);
    var name = trim_(toPrimitiveStr_(u.name));
    var active = parseActive_(u.active);
    // password: nếu cập nhật mà bỏ trống thì GIỮ mật khẩu cũ.
    var hasPassword =
      u.password !== undefined &&
      u.password !== null &&
      toPrimitiveStr_(u.password) !== "";
    var password = hasPassword
      ? makePasswordHash_(toPrimitiveStr_(u.password)) // mật khẩu mới → băm
      : existing
        ? String(existing.password) // giữ nguyên (đã hash/legacy)
        : "";

    // email: bỏ trống khi cập nhật → GIỮ email cũ.
    var hasEmail =
      u.email !== undefined &&
      u.email !== null &&
      toPrimitiveStr_(u.email) !== "";
    var email = hasEmail
      ? trim_(toPrimitiveStr_(u.email))
      : existing
        ? String(existing.email || "")
        : "";

    // CHẶN hạ quyền / khoá admin cuối cùng (chống tự khoá mình ra ngoài) —
    // cùng logic đếm admin như deleteUser. Nếu target hiện là admin và
    // thay đổi này biến nó thành non-admin HOẶC inactive, mà nó là admin
    // hoạt động DUY NHẤT thì từ chối.
    if (
      existing &&
      existing.role === "root" &&
      (role !== "root" || !active)
    ) {
      var usableRoots = 0;
      for (var a = 0; a < model.rows.length; a++) {
        if (model.rows[a].role === "root" && model.rows[a].active) {
          usableRoots++;
        }
      }
      if (usableRoots <= 1 && existing.active) {
        return { ok: false, error: "Không thể hạ quyền/khoá root cuối cùng" };
      }
    }

    var created;
    if (existing) {
      var r = existing.rowIndex; // 1-based row trong sheet
      sheet.getRange(r, col.username + 1).setValue(username);
      sheet.getRange(r, col.password + 1).setValue(password);
      sheet.getRange(r, col.role + 1).setValue(role);
      sheet.getRange(r, col.ids + 1).setValue(idsStr);
      sheet.getRange(r, col.name + 1).setValue(name);
      if (col.email >= 0) sheet.getRange(r, col.email + 1).setValue(email);
      sheet.getRange(r, col.active + 1).setValue(active); // boolean thật → giữ checkbox
      created = false;
    } else {
      var width = model.width;
      var rowArr = new Array(width);
      for (var i = 0; i < width; i++) rowArr[i] = "";
      rowArr[col.username] = username;
      rowArr[col.password] = password;
      rowArr[col.role] = role;
      rowArr[col.ids] = idsStr;
      rowArr[col.name] = name;
      if (col.email >= 0) rowArr[col.email] = email;
      rowArr[col.active] = active; // boolean thật → giữ checkbox
      sheet.appendRow(rowArr);
      created = true;
    }
    return { ok: true, saved: username, created: created };
  } finally {
    lock.releaseLock();
  }
}

function actionDeleteUser_(body) {
  var lock = LockService.getScriptLock();
  if (!tryLock_(lock)) {
    return { ok: false, error: "Hệ thống đang bận, vui lòng thử lại" };
  }
  try {
    // Admin gate TRƯỚC validation field → caller chưa xác thực nhận
    // NO_PERMISSION_MSG đồng nhất.
    var gate = requireAdmin_(body);
    if (!gate.ok) return gate;

    var username = trim_(toPrimitiveStr_(body && body.username));
    if (!username) return { ok: false, error: "Thiếu username" };

    var model = gate.model;
    var target = findUser_(model, username);
    if (!target)
      return { ok: false, error: "Không tìm thấy user: " + username };

    // CHẶN xoá root cuối cùng để tránh khoá chính mình ra ngoài (mất quản lý).
    if (target.role === "root") {
      var rootCount = 0;
      for (var i = 0; i < model.rows.length; i++) {
        if (model.rows[i].role === "root") rootCount++;
      }
      if (rootCount <= 1) {
        return { ok: false, error: "Không thể xoá root cuối cùng" };
      }
    }

    model.sheet.deleteRow(target.rowIndex);
    return { ok: true, deleted: username };
  } finally {
    lock.releaseLock();
  }
}

/* -------------------------- Admin gate ----------------------------- */

/* Trả {ok:true, model} nếu admin hợp lệ, ngược lại {ok:false,error}.
 * Re-validate adminUser+adminPassword trên MỌI lần gọi và yêu cầu
 * role==="admin". Đọc sheet 1 lần, trả luôn model để tái dùng.
 * Mọi điều kiện đánh giá thành boolean trước (không short-circuit) và
 * so mật khẩu constant-time để giảm enumeration qua timing. */
/* Chuẩn hoá role: root / admin / user (giá trị khác → user). */
function normalizeRole_(v) {
  var r = trim_(v).toLowerCase();
  return r === "root" ? "root" : r === "admin" ? "admin" : "user";
}

function requireAdmin_(body) {
  var adminUser = trim_(toPrimitiveStr_(body && body.adminUser));
  var adminPassword = toPrimitiveStr_(body && body.adminPassword);
  var model = readAccounts_();
  var rec = findUser_(model, adminUser);

  var notFound = !rec;
  var inactive = rec ? !rec.active : true;
  var badPass = rec
    ? !verifyPassword_(rec.password, adminPassword)
    : true;
  // CHỈ root được quản lý tài khoản (thêm/xóa nhân viên + khách hàng).
  var notRoot = rec ? rec.role !== "root" : true;
  if (notFound || inactive || badPass || notRoot) {
    return { ok: false, error: NO_PERMISSION_MSG };
  }
  return { ok: true, model: model };
}

/* --------------------------- Sheet I/O ----------------------------- */

/* Đọc toàn bộ tab Accounts một lần. Trả model:
 *   { sheet, col:{username,password,role,ids,name,active}, width,
 *     rows:[{username,password,role,ids,name,active,rowIndex}] }
 * rowIndex là số hàng 1-based trong sheet (để ghi/xoá). */
function readAccounts_() {
  if (!ACCOUNTS_SHEET_ID || ACCOUNTS_SHEET_ID === "<FILL_ME>") {
    throw new Error(
      "Chưa cấu hình ACCOUNTS_SHEET_ID. Hãy điền ID Google Sheet accounts.",
    );
  }
  var ss;
  try {
    ss = SpreadsheetApp.openById(ACCOUNTS_SHEET_ID);
  } catch (e) {
    throw new Error(
      "Không mở được sheet (ACCOUNTS_SHEET_ID sai hoặc không có quyền).",
    );
  }
  var sheet = ss.getSheetByName(ACCOUNTS_TAB);
  if (!sheet) {
    throw new Error('Không tìm thấy tab "' + ACCOUNTS_TAB + '" trong sheet.');
  }

  var values = sheet.getDataRange().getValues();
  if (!values || values.length === 0) {
    throw new Error('Tab "' + ACCOUNTS_TAB + '" trống (thiếu hàng tiêu đề).');
  }

  var header = values[0];
  var col = resolveColumns_(header);
  // Bao phủ MỌI index cột đã resolve để rowArr (nhánh append) đủ rộng.
  var width =
    Math.max(
      header.length,
      col.username,
      col.password,
      col.role,
      col.ids,
      col.name,
      col.active,
      col.email,
      5, // floor index → width tối thiểu 6
    ) + 1;

  var rows = [];
  for (var r = 1; r < values.length; r++) {
    var row = values[r];
    var username = trim_(cell_(row, col.username));
    // Bỏ qua hàng trống hoàn toàn (không có username).
    if (username === "") continue;
    rows.push({
      username: username,
      password: cell_(row, col.password),
      role: normalizeRole_(cell_(row, col.role)),
      ids: cell_(row, col.ids),
      name: trim_(cell_(row, col.name)),
      email: trim_(cell_(row, col.email)),
      active: parseActive_(cell_(row, col.active)),
      rowIndex: r + 1, // 1-based
    });
  }

  return { sheet: sheet, col: col, width: width, rows: rows };
}

/* Dò index cột theo header (không phân biệt hoa thường); thiếu thì
 * fallback theo thứ tự cố định A..F. Trả index 0-based. */
function resolveColumns_(header) {
  var idx = {};
  for (var i = 0; i < header.length; i++) {
    var key = String(header[i] || "")
      .trim()
      .toLowerCase();
    if (key && idx[key] === undefined) idx[key] = i;
  }
  function pick(name, fallback) {
    return idx[name] !== undefined ? idx[name] : fallback;
  }
  return {
    username: pick("username", 0),
    password: pick("password", 1),
    role: pick("role", 2),
    ids: pick("ids", 3),
    name: pick("name", 4),
    email: pick("email", -1), // -1 = sheet chưa có cột email (tuỳ chọn)
    active: pick("active", 5),
  };
}

function cell_(row, idx) {
  if (idx == null || idx < 0 || idx >= row.length) return "";
  return row[idx];
}

/* Tìm user theo username (không phân biệt hoa thường). */
function findUser_(model, username) {
  var target = trim_(username).toLowerCase();
  if (!target) return null;
  for (var i = 0; i < model.rows.length; i++) {
    if (String(model.rows[i].username).trim().toLowerCase() === target) {
      return model.rows[i];
    }
  }
  return null;
}

/* --------------------------- Helpers ------------------------------- */

function trim_(v) {
  return v == null ? "" : String(v).trim();
}

/* Mảng? (Array.isArray có thể không có trên engine cũ → dùng toString). */
function isArray_(v) {
  return Object.prototype.toString.call(v) === "[object Array]";
}

/* Ép giá trị về chuỗi CHỈ khi là primitive; object/array → "" (từ chối
 * coi như rỗng) để tránh dựa vào coercion tình cờ ([object Object], ...). */
function toPrimitiveStr_(v) {
  if (v == null) return "";
  var t = typeof v;
  if (t === "string") return v;
  if (t === "number" || t === "boolean") return String(v);
  return ""; // object/array/function → không hợp lệ, coi như rỗng
}

/* So sánh chuỗi kiểu constant-time: không thoát sớm theo độ dài/nội dung
 * để giảm rò rỉ thông tin qua timing (best-effort trên GAS). */
function constantTimeEquals_(a, b) {
  var sa = String(a == null ? "" : a);
  var sb = String(b == null ? "" : b);
  var len = Math.max(sa.length, sb.length);
  var diff = sa.length ^ sb.length;
  for (var i = 0; i < len; i++) {
    var ca = i < sa.length ? sa.charCodeAt(i) : 0;
    var cb = i < sb.length ? sb.charCodeAt(i) : 0;
    diff |= ca ^ cb;
  }
  return diff === 0;
}

/* ----------------------- Băm mật khẩu (salt) ----------------------- */

/* SHA-256(s) → chuỗi hex. */
function sha256Hex_(s) {
  var bytes = Utilities.computeDigest(
    Utilities.DigestAlgorithm.SHA_256,
    String(s),
    Utilities.Charset.UTF_8,
  );
  var hex = "";
  for (var i = 0; i < bytes.length; i++) {
    var b = (bytes[i] + 256) % 256; // byte có dấu → 0..255
    hex += (b < 16 ? "0" : "") + b.toString(16);
  }
  return hex;
}

/* Tạo chuỗi lưu sheet: "sha256$<salt>$<hash>" (salt riêng mỗi lần). */
function makePasswordHash_(plain) {
  var salt = Utilities.getUuid().replace(/-/g, "");
  return "sha256$" + salt + "$" + sha256Hex_(salt + String(plain));
}

/* Chuỗi có ĐÚNG định dạng hash "sha256$<salt>$<64 hex>" không? Chặt hơn việc
 * chỉ xét tiền tố "sha256$" → tránh nhầm plaintext (hiếm) bắt đầu bằng "sha256$"
 * thành hash hỏng (khiến không đăng nhập được + bị migrate bỏ qua). */
function isHashed_(s) {
  s = String(s == null ? "" : s);
  if (s.indexOf("sha256$") !== 0) return false;
  var parts = s.split("$");
  return parts.length === 3 && /^[0-9a-f]{64}$/.test(parts[2]);
}

/* So khớp mật khẩu: hỗ trợ cả hash "sha256$salt$hash" lẫn plaintext cũ. */
function verifyPassword_(stored, plain) {
  var s = String(stored == null ? "" : stored);
  if (isHashed_(s)) {
    var parts = s.split("$"); // ["sha256", salt, hash]
    return constantTimeEquals_(sha256Hex_(parts[1] + String(plain)), parts[2]);
  }
  // Tương thích ngược: plaintext (kể cả chuỗi tình cờ bắt đầu "sha256$" nhưng
  // không đúng định dạng hash).
  return constantTimeEquals_(s, plain);
}

/* Khoá script lock có timeout; trả true nếu lấy được. */
function tryLock_(lock) {
  try {
    return lock.tryLock(15000); // chờ tối đa 15s
  } catch (e) {
    return false;
  }
}

/* Parse ids robust: tách trên /[\s,;]+/, trim, bỏ rỗng, dedupe; giữ "*". */
function parseIds_(raw) {
  var parts = [];
  if (raw == null) return parts;
  if (isArray_(raw)) {
    parts = raw;
  } else {
    parts = String(raw).split(/[\s,;]+/);
  }
  var out = [];
  var seen = {};
  for (var i = 0; i < parts.length; i++) {
    var token = trim_(parts[i]);
    if (token === "") continue;
    if (seen[token]) continue;
    seen[token] = true;
    out.push(token);
  }
  return out;
}

/* ids (array hoặc string) -> chuỗi sạch nối bằng dấu phẩy để lưu sheet. */
function normalizeIdsToString_(raw) {
  return parseIds_(raw).join(",");
}

/* TRUE/FALSE robust: blank/"" = active (true). */
function parseActive_(v) {
  if (v === true) return true;
  if (v === false) return false;
  var s = trim_(v).toLowerCase();
  if (s === "") return true; // để trống = đang hoạt động
  if (s === "false" || s === "0" || s === "no" || s === "n") return false;
  return true;
}

/* ====================================================================
 *  MIGRATE — chạy TAY 1 lần trong editor (Run → migratePasswordsToHash).
 *  Băm mọi mật khẩu plaintext còn lại trong tab Accounts → "sha256$...".
 *  An toàn chạy lại (bỏ qua ô đã hash). Sau khi chạy, sheet KHÔNG còn
 *  mật khẩu thật.
 * ==================================================================== */
function migratePasswordsToHash() {
  var lock = LockService.getScriptLock();
  if (!tryLock_(lock)) {
    Logger.log("Hệ thống đang bận, thử lại.");
    return;
  }
  try {
    var model = readAccounts_();
    var n = 0;
    for (var i = 0; i < model.rows.length; i++) {
      var rec = model.rows[i];
      var stored = String(rec.password == null ? "" : rec.password);
      if (stored === "" || isHashed_(stored)) continue; // rỗng/đã hash đúng định dạng
      model.sheet
        .getRange(rec.rowIndex, model.col.password + 1)
        .setValue(makePasswordHash_(stored));
      n++;
    }
    Logger.log("Đã băm " + n + " mật khẩu plaintext.");
  } finally {
    lock.releaseLock();
  }
}

/* ====================================================================
 *  TEST HELPERS — chạy trực tiếp trong GAS editor (Run → chọn hàm).
 *  Dùng fake e.postData để mô phỏng request từ app. Xem log ở
 *  View → Logs / Executions.
 * ==================================================================== */

function fakePost_(body) {
  return { postData: { contents: JSON.stringify(body) }, parameter: {} };
}

function test_doGet() {
  Logger.log(doGet({ parameter: {} }).getContent());
}

function test_hash() {
  var h = makePasswordHash_("abc");
  Logger.log("hash    : " + h);
  Logger.log("verify abc   : " + verifyPassword_(h, "abc")); // true
  Logger.log("verify xyz   : " + verifyPassword_(h, "xyz")); // false
  Logger.log("verify legacy: " + verifyPassword_("plainpw", "plainpw")); // true
  Logger.log("salt khác    : " + (makePasswordHash_("abc") !== makePasswordHash_("abc"))); // true
}

function test_login() {
  // Sửa username/password cho khớp dữ liệu trong sheet của bạn.
  var res = doPost(
    fakePost_({ action: "login", username: "admin", password: "123456" }),
  );
  Logger.log("login OK   -> " + res.getContent());

  var bad = doPost(
    fakePost_({ action: "login", username: "admin", password: "sai" }),
  );
  Logger.log("login FAIL -> " + bad.getContent()); // phải là LOGIN_FAIL_MSG
}

function test_listUsers() {
  var res = doPost(
    fakePost_({
      action: "listUsers",
      adminUser: "admin",
      adminPassword: "123456",
    }),
  );
  Logger.log("listUsers -> " + res.getContent()); // không có password
}

function test_saveUser() {
  // Tạo mới rồi cập nhật (bỏ trống password để giữ mật khẩu cũ).
  var create = doPost(
    fakePost_({
      action: "saveUser",
      adminUser: "admin",
      adminPassword: "123456",
      user: {
        username: "lab01",
        password: "abc123",
        role: "user",
        ids: ["RPL02007", "RPL02100"],
        name: "Nguyễn Văn A",
        active: true,
      },
    }),
  );
  Logger.log("saveUser create -> " + create.getContent());

  var update = doPost(
    fakePost_({
      action: "saveUser",
      adminUser: "admin",
      adminPassword: "123456",
      user: {
        username: "lab01",
        ids: "RPL02007; RPL02100 RPL02100", // sẽ được dedupe/normalize
        name: "Nguyễn Văn A (updated)",
        active: true,
      },
    }),
  );
  Logger.log("saveUser update -> " + update.getContent());
}

function test_deleteUser() {
  var res = doPost(
    fakePost_({
      action: "deleteUser",
      adminUser: "admin",
      adminPassword: "123456",
      username: "lab01",
    }),
  );
  Logger.log("deleteUser -> " + res.getContent());
}
