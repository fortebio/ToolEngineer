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
 *   | username | password | role  | ids              | name        | active |
 *   |----------|----------|-------|------------------|-------------|--------|
 *   | admin    | 123456   | admin | *                | Quản trị    | TRUE   |
 *   | lab01    | abc123   | user  | RPL02007,RPL0210 | Nguyễn Văn A| TRUE   |
 *
 *   - role  ∈ {"admin","user"}.
 *   - ids   = các mã máy ngăn cách bởi dấu phẩy / chấm phẩy / khoảng
 *             trắng. admin có thể dùng "*" nghĩa là TẤT CẢ máy.
 *   - active= TRUE/FALSE (để trống = đang hoạt động). FALSE = khoá.
 *   Cột được dò theo TÊN header (không phân biệt hoa thường); nếu không
 *   thấy header sẽ fallback theo thứ tự cố định A..F như trên.
 *
 *  --------------------------- API -----------------------------------
 *  doGet  : health-check → {ok:true, service:"accounts"}.
 *  doPost : body JSON, dispatch theo body.action (POST để mật khẩu KHÔNG
 *           nằm trên URL). Actions:
 *     - login      {username, password}
 *     - listUsers  {adminUser, adminPassword}                (admin-gated)
 *     - saveUser   {adminUser, adminPassword, user:{...}}    (admin-gated)
 *     - deleteUser {adminUser, adminPassword, username}      (admin-gated)
 *   Thành công: {ok:true, ...}. Thất bại: {ok:false, error:"..."}.
 *
 *  ------------------------ SECURITY NOTE ----------------------------
 *  Mật khẩu lưu PLAINTEXT trong sheet RIÊNG TƯ (chỉ chủ sở hữu xem được);
 *  client không bao giờ nhận mật khẩu của user khác; nên hash mật khẩu
 *  (vd SHA-256 + salt) ở bản sau để tăng an toàn.
 * ===================================================================== */

var ACCOUNTS_SHEET_ID = "<FILL_ME>"; // <-- điền ID Google Sheet accounts
var ACCOUNTS_TAB = "Accounts";
var LOGIN_FAIL_MSG = "Sai tài khoản hoặc mật khẩu"; // dùng CHUNG cho mọi lỗi login
var NO_PERMISSION_MSG = "Không có quyền";

/* ------------------------- HTTP entrypoints ------------------------ */

function doGet(e) {
  var p = (e && e.parameter) || {};
  try {
    return reply_({ ok: true, service: "accounts" }, p.callback);
  } catch (err) {
    return reply_(
      { ok: false, error: String((err && err.message) || err) },
      p.callback,
    );
  }
}

function doPost(e) {
  var callback = e && e.parameter ? e.parameter.callback : null;
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return reply_({ ok: false, error: "Thiếu dữ liệu POST" }, callback);
    }
    var body = JSON.parse(e.postData.contents);
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
      default:
        out = { ok: false, error: "action không hợp lệ: " + action };
    }
    return reply_(out, callback);
  } catch (err) {
    return reply_(
      { ok: false, error: String((err && err.message) || err) },
      callback,
    );
  }
}

/* JSON / JSONP response helper (giống style getData.js). */
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

/* --------------------------- Actions ------------------------------- */

function actionLogin_(body) {
  var username = trim_((body && body.username) || "");
  var password = String((body && body.password) != null ? body.password : "");
  if (!username) return { ok: false, error: LOGIN_FAIL_MSG };

  var model = readAccounts_();
  var rec = findUser_(model, username);
  // Phản hồi GIỐNG NHAU cho: không có user / sai mật khẩu / bị khoá.
  if (!rec) return { ok: false, error: LOGIN_FAIL_MSG };
  if (!rec.active) return { ok: false, error: LOGIN_FAIL_MSG };
  if (String(rec.password) !== password) {
    return { ok: false, error: LOGIN_FAIL_MSG };
  }

  var ids = parseIds_(rec.ids);
  var allowAll = rec.role === "admin" || ids.indexOf("*") >= 0;
  return {
    ok: true,
    username: rec.username,
    name: rec.name,
    role: rec.role,
    ids: ids,
    allowAll: allowAll,
  };
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
      active: rec.active,
    }; // KHÔNG bao giờ trả password
  });
  return { ok: true, users: users };
}

function actionSaveUser_(body) {
  var u = (body && body.user) || {};
  var username = trim_(u.username || "");
  if (!username) return { ok: false, error: "Thiếu username" };

  var role = trim_(u.role || "").toLowerCase();
  if (!role) role = "user";
  if (role !== "admin" && role !== "user") {
    return { ok: false, error: "role không hợp lệ (chỉ admin/user)" };
  }

  var lock = LockService.getScriptLock();
  if (!tryLock_(lock)) {
    return { ok: false, error: "Hệ thống đang bận, vui lòng thử lại" };
  }
  try {
    // Re-validate admin BÊN TRONG lock (đọc lại sheet để nhất quán).
    var gate = requireAdmin_(body);
    if (!gate.ok) return gate;

    var model = gate.model;
    var sheet = model.sheet;
    var col = model.col;
    var existing = findUser_(model, username);

    var idsStr = normalizeIdsToString_(u.ids);
    var name = trim_(u.name || "");
    var active = parseActive_(u.active);
    // password: nếu cập nhật mà bỏ trống thì GIỮ mật khẩu cũ.
    var hasPassword =
      u.password !== undefined && u.password !== null && String(u.password) !== "";
    var password = hasPassword
      ? String(u.password)
      : existing
        ? String(existing.password)
        : "";

    var created;
    if (existing) {
      var r = existing.rowIndex; // 1-based row trong sheet
      sheet.getRange(r, col.username + 1).setValue(username);
      sheet.getRange(r, col.password + 1).setValue(password);
      sheet.getRange(r, col.role + 1).setValue(role);
      sheet.getRange(r, col.ids + 1).setValue(idsStr);
      sheet.getRange(r, col.name + 1).setValue(name);
      sheet.getRange(r, col.active + 1).setValue(active ? "TRUE" : "FALSE");
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
      rowArr[col.active] = active ? "TRUE" : "FALSE";
      sheet.appendRow(rowArr);
      created = true;
    }
    return { ok: true, saved: username, created: created };
  } finally {
    lock.releaseLock();
  }
}

function actionDeleteUser_(body) {
  var username = trim_((body && body.username) || "");
  if (!username) return { ok: false, error: "Thiếu username" };

  var lock = LockService.getScriptLock();
  if (!tryLock_(lock)) {
    return { ok: false, error: "Hệ thống đang bận, vui lòng thử lại" };
  }
  try {
    var gate = requireAdmin_(body);
    if (!gate.ok) return gate;

    var model = gate.model;
    var target = findUser_(model, username);
    if (!target) return { ok: false, error: "Không tìm thấy user: " + username };

    // CHẶN xoá admin cuối cùng để tránh khoá chính mình ra ngoài.
    if (target.role === "admin") {
      var adminCount = 0;
      for (var i = 0; i < model.rows.length; i++) {
        if (model.rows[i].role === "admin") adminCount++;
      }
      if (adminCount <= 1) {
        return { ok: false, error: "Không thể xoá admin cuối cùng" };
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
 * role==="admin". Đọc sheet 1 lần, trả luôn model để tái dùng. */
function requireAdmin_(body) {
  var adminUser = trim_((body && body.adminUser) || "");
  var adminPassword = String(
    (body && body.adminPassword) != null ? body.adminPassword : "",
  );
  var model = readAccounts_();
  var rec = findUser_(model, adminUser);
  if (
    !rec ||
    !rec.active ||
    String(rec.password) !== adminPassword ||
    rec.role !== "admin"
  ) {
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
  var width = Math.max(header.length, col.username + 1, col.active + 1, 6);

  var rows = [];
  for (var r = 1; r < values.length; r++) {
    var row = values[r];
    var username = trim_(cell_(row, col.username));
    // Bỏ qua hàng trống hoàn toàn (không có username).
    if (username === "") continue;
    rows.push({
      username: username,
      password: cell_(row, col.password),
      role: trim_(cell_(row, col.role)).toLowerCase() === "admin"
        ? "admin"
        : "user",
      ids: cell_(row, col.ids),
      name: trim_(cell_(row, col.name)),
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
    var key = String(header[i] || "").trim().toLowerCase();
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
  if (Object.prototype.toString.call(raw) === "[object Array]") {
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

function test_login() {
  // Sửa username/password cho khớp dữ liệu trong sheet của bạn.
  var res = doPost(fakePost_({ action: "login", username: "admin", password: "123456" }));
  Logger.log("login OK   -> " + res.getContent());

  var bad = doPost(fakePost_({ action: "login", username: "admin", password: "sai" }));
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
