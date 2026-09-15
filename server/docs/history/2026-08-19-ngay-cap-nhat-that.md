# 2026-08-19 — Ngày cập nhật THẬT của khách (`fw_log.json` + `GET /devices/{id}/fw-log`)

## Vì sao

Tính năng "Lịch sử cập nhật" làm sáng nay suy ngày từ `sessions.version`: ngày hiện ra là
**lần đo ĐẦU TIÊN báo bản đó**. Chủ dự án nói thẳng thứ mình cần:

> "vì tôi muốn mỗi lần cập nhật thì sẽ biết được ngày update của khách chứ không phải ngày
> up lên mới nhất"

Phép suy cũ hỏng ở hai chỗ, và cả hai đều **im lặng**:

* Khách nạp thứ Sáu, thứ Ba mới chạy mẫu → lịch sử ghi thứ Ba. Lệch 4 ngày, không dấu hiệu.
* Khách nạp xong **chưa chạy mẫu lần nào** → **không có dòng nào cả**. Máy đã lên bản mới mà
  bảng nói chưa từng cập nhật.

Từ v2.4.5 firmware tự khai version ở `/ota/check?ver=` ngay khi khởi động lại sau khi nạp —
đó mới là mốc đúng. Nhưng `fw_seen.json` **ghi đè mỗi lượt gọi**, nên mốc đó sống được vài
phút rồi mất.

## Server

* **`fw_log.json`** (trong `OTA_DIR`) — `{id_device: [{version, at}, …]}`, **chỉ nối thêm khi
  version ĐỔI**, trần 50 mục/máy. Máy poll `/ota/check` liên tục nên không lọc là file phình vô
  hạn mà chẳng thêm thông tin.
* **Ghi nhật ký TRƯỚC `fw_seen`**: crash giữa hai bước thì mất cái ghi đè được (`seen`), không
  mất cái không dựng lại được (`log`). Cả hai qua `_atomic_json` (`os.replace`).
* **`GET /devices/{device}/fw-log`** (Bearer) — mới nhất trước.
* Hai lỗi tìm được lúc soát, sửa luôn:
  * `_fw_entry` — `fw_seen.json` sửa tay được y như `target.json`; một mục méo (chuỗi trần thay
    vì dict) làm `/devices` ném 500 ⇒ **trắng cả bảng Trạng thái máy**. Nay tha thứ, bỏ qua mục hỏng.
  * `_fw_newer` — bản tự khai chỉ được đè lên `sessions.version` khi nó **mới hơn `last_seen`**.
    Không có phép so này thì một mục `fw_seen` cũ ghim version cũ đè lên phiên đo mới.

⚠️ **Giới hạn phải nói ra**: nhật ký chỉ có dữ liệu **từ lúc firmware ≥ v2.4.5 và server này
lên**. Phần trước đó không tồn tại ở đâu cả — app phải suy từ phiên đo và **nói rõ là suy đoán**.

## App

* `FirmwareStint.installedAt` (`services/firmware_history.dart`) — mốc thật, `null` = phải suy.
  Kèm `updatedAt` (mốc thật nếu có) và `dateIsExact`.
* `mergeFirmwareLog(log, stints)` — **nhật ký là sự thật cho khoảng nó phủ; phần suy đoán chỉ
  dùng cho quãng CŨ HƠN mốc sớm nhất trong nhật ký.** Trộn hai nguồn trong cùng một khoảng thì
  MỘT lần cập nhật hiện thành HAI dòng với hai ngày khác nhau — đủ để người vận hành hết tin
  cả bảng. Số lần đo vẫn lấy từ phiên (nhật ký không đếm), gả sang dòng chính xác cùng version;
  bỏ qua là mọi dòng mới hiện "0 lần đo" trong khi máy chạy cả trăm mẫu.
* `fromVersion` **tính lại trên danh sách đã ghép** — chỗ giáp ranh hai nguồn không có nó thì
  phép suy OTA/tay đọc nhầm (đúng kiểu lỗi dữ liệu thật đã bác một lần: `v2.3.6 → v2.4.5`).
* `FbtApi.fwLog()` **nuốt mọi lỗi, trả rỗng** — server chưa deploy route này trả **405** (catch-all
  `POST /{path}` khớp path, sai method). Ném lỗi ở đây là hỏng cả hộp thoại vì thiếu phần làm giàu.
* Hộp thoại: dòng có **`≈`** là ngày suy đoán, không có `≈` là ngày cập nhật thật. Ghi chú chân
  hộp nói cả hai.

## Kiểm

* Server: 44 test, **4 test mới**, gieo 4 lỗi — bắt đủ 4.
* App: 76 test, **7 test mới** cho `mergeFirmwareLog`, gieo 6 lỗi — bắt đủ 6.

## Vòng soát đối kháng sau khi deploy — 3 lỗi THẬT, đều do đợt sửa này đẻ ra

Cho 5 góc soát chạy song song, mỗi phát hiện bị 3 người cố bác. ⚠️ **28/56 agent chết vì hết
hạn mức phiên** nên chỉ 2 phát hiện được thẩm định thật; phần còn lại tự kiểm bằng test.

1. **`_fw_read` — file nhật ký hỏng KHÔNG được ghi đè** (`_fw_log`/`_fw_seen` trả `{}` khi
   `JSONDecodeError`, rồi `_fw_report` ghi đè trọn file ⇒ **một lượt check của MỘT máy xoá sổ
   mốc của cả 109 máy**, im lặng). Nay phân biệt `{}` (chưa có file) với `None` (có mà hỏng);
   hỏng thì bỏ ghi + kêu trong journal. Ca **JSON hợp lệ nhưng không phải dict** (`[]`, `null`)
   cũng tính là hỏng — bắt theo `JSONDecodeError` thôi thì ca đó lọt (gieo lỗi mới phát hiện).
2. **`_FW_MAX_DEVICES = 500` — trần SỐ MÁY.** `_FW_LOG_MAX` chỉ chặn số mốc *trên mỗi máy*.
   `device` là query param tuỳ ý, `/ota/check` **không qua ratelimit**, và token thiết bị nằm
   trong 4 KB đầu mọi `.bin` ⇒ bơm mã máy ngẫu nhiên là hai file phình vô hạn, mà mỗi lượt
   check đọc+ghi **trọn file** trong `_FW_LOCK` của tiến trình một-worker ⇒ O(n²) làm nghẹn
   đúng đường sửa từ xa duy nhất. Máy **đã có** trong nhật ký vẫn ghi được (trần không khoá
   fleet thật).
3. **`mergeFirmwareLog` NUỐT quãng nhật ký không phủ** (phía app) — ca thật của RPL02013 cùng
   ngày: máy chạy v2.4.5 (tự khai) rồi bị **nạp tay xuống v2.4.4**, mà v2.4.4 không gửi `?ver=`
   nên nhật ký câm từ đó. Cắt cứng theo mốc nhật ký ⇒ **bản máy ĐANG chạy biến mất khỏi lịch
   sử**. Nay gộp cả hai nguồn rồi **xếp lại theo thời gian** và gộp mốc trùng liền nhau.
   - Luật rút ra: **nguồn nào NÓI SỚM HƠN thì thắng.** Mốc nhật ký chỉ là "ngày cập nhật thật"
     khi nó là thứ ĐẦU TIÊN biết tới bản đó. Có lần đo báo bản ấy trước ⇒ máy đã chạy nó từ
     trước, mốc nhật ký chỉ là lượt poll ⇒ giữ ngày suy đoán, giữ dấu `≈`.

## Phát hiện KHÔNG sửa (có chủ ý)

`device` do client tự khai, không ràng buộc danh tính ⇒ ai cầm token đọc có thể khai version
giả cho máy người khác. **Thật, nhưng không thêm quyền gì mới**: cùng token đó đã POST được
kết quả đo giả, làm hỏng đúng cột ấy — xem gotcha bảo mật `/auth` trong `CLAUDE.md`. Sửa đúng
là cấp token theo từng máy, việc lớn hơn hẳn và đã nằm trong sổ nợ.

## Còn lại

* **Chưa deploy `server/app/main.py`.** Phải scp + restart (AI chỉ được scp + kiểm).
  Đối chiếu cả gói trước khi restart: `md5sum ~/fbt_server/app/*.py`.
* `target.json` vẫn đang armed `fbt_v2.4.4.bin` trong khi RPL02013 chạy v2.4.5 → **bẫy hạ cấp
  ngược**, và chính nó là thứ kích hoạt lỗi `fw_seen` cũ. `DELETE /ota/target` hoặc bỏ chọn trong app.
