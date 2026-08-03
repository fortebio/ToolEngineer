# Pha 1: nhúng UI vào firmware, bỏ `serveStatic` (2026-07-29)

Thực hiện [docs/plan/2026-07-28-ota-fleet-upgrade-243.md](../plan/2026-07-28-ota-fleet-upgrade-243.md)
**Pha 1 (mục 6-9)**. Tiếp sau [Pha 0](2026-07-29-phase0-ota-fleet-upgrade.md).

## Vấn đề: hai artifact, một đường OTA

Dashboard phục vụ từ LittleFS ⇒ mỗi máy cần **hai** ảnh khớp nhau (`firmware.bin` +
`littlefs.bin`), mà **không có cách đẩy ảnh thứ hai qua mạng an toàn**:

- `_verifyHeader()` luôn `return true` cho `U_SPIFFS` (`Updater.cpp:243-245`)
- Trick giữ 16 byte đầu để ảnh cụt không boot được **chỉ có ở `U_FLASH`** (`Updater.cpp:185-202`)
- `raw.githubusercontent.com` không gửi `x-MD5`
- Partition `spiffs` **không A/B, không rollback**

⇒ Bất kỳ body HTTP 200 nào — kể cả trang HTML rate-limit của GitHub — sẽ được ghi thẳng lên
partition rồi **vẫn mount được** và **vẫn phục vụ file cụt**.

Thêm nữa, nội dung `spiffs` của máy ngoài đồng là **câu không ai trả lời được**: máy 2.4.2 không
hề mount LittleFS (web của nó là PROGMEM `src/index.h`), nên `spiffs` có thể trống, có thể còn
ảnh demo 2020. Trống → `/` **404 trắng** (SoftAP thì `ERR_TOO_MANY_REDIRECTS` ngay sau khi quét
QR). Có ảnh cũ → mount **im lặng**, browser nhận trang demo, và trang đó **vẫn subscribe được
`/events`** rồi vẽ chart **lệch một series** (key `i` bị vẽ thành `#1`) — biểu đồ khuếch đại
**trông hợp lý mà sai nhãn**, trên máy chẩn đoán.

## Cách làm: `.rodata`, không phải filesystem

`tools/pio_gzip_data.py` (mở rộng, **không thêm script mới**) nay chạy ở **module scope** — trước
đó gắn vào `AddPreAction("$BUILD_DIR/littlefs.bin")`, mà target đó không còn được build nữa. Nó:

1. gzip 3 asset text như cũ (`mtime=0` → output byte-identical cho input không đổi),
2. sinh **`src/webAssets.h`**: 5 mảng `static const uint8_t` + `#define WEB_ASSETS_ETAG` =
   16 hex đầu của SHA-1 nội dung. **Chỉ ghi đè khi nội dung khác** — chạm mtime là bắt
   `webDashboard.cpp` (786 KB header) biên dịch lại mỗi build.

`webDashboard.cpp` thay `serveStatic` bằng bảng `kWebAssets[]` `{path, ptr, len, mime, gz}` +
vòng `dashServer.on(...)`, **giữ nguyên vị trí** (sau API routes, trước `onNotFound` — GOTCHA 12).
Mỗi asset: `beginResponse(200, mime, ptr, len)` → `Content-Encoding: gzip` (khi gz) →
`Cache-Control: no-cache` → `ETag`, và **304** khi `If-None-Match` khớp.

Đo thật: flash **70.5% → 74.8%** (2 355 945 → 2 500 089 B, +144 144), **RAM không đổi**
(77 204 B cả trước lẫn sau) — response stream thẳng từ flash.

**Ba cái bẫy:**

- **Tuyệt đối không truyền template callback** vào `beginResponse` — `_fillBufferAndProcessTemplates`
  (`WebResponses.cpp:494-496`) quét ký tự `%` và **sửa bytes gzip tại chỗ**.
- **ETag không được bỏ**: bỏ `serveStatic` là bỏ luôn ETag/304 của thư viện
  (`WebHandlers.cpp:210-245`). Thiếu nó thì mỗi lần mở trang gửi lại 147 739 B trên thiết bị mà
  **khối heap liền mạch** mới là thứ khan hiếm; tệ hơn, browser giữ `script.js` cũ → **đúng lớp
  lỗi chart-sai-nhãn**, chỉ khác đường vào. Một ETag chung cho cả bộ (chúng đi cùng version).
- **Không còn lưới an toàn**: `serveStatic` tự phục vụ file mới trong `data/`; nay thêm asset mà
  quên đăng ký = **404 trên máy thật**, chỉ lộ sau khi nạp. Đó là lý do có guard dưới đây.

`src/webAssets.h` **gitignored** (sinh lại mỗi build từ `data/`).

## `/otaupload?md5=` — bịt đường brick duy nhất

`Update.end(true)` đặt `_size = progress()`, tức **"bao nhiêu byte tới nơi" được tính là cả ảnh**
⇒ một file `.bin` đứt giữa chừng **vẫn được đánh dấu bootable**. Nay `POST /otaupload?md5=<32 hex>`
gọi `Update.setMD5()` ngay sau `Update.begin` → `Update.end()` so digest và từ chối.

Giá trị **sai định dạng thì từ chối** (`otaUpFail`), không im lặng bỏ qua: ai đã xin verify thì
không được nhận về "không verify gì cả".

## OTA từ GitHub: kiểm busy LẠI, và reboot khi rảnh

Web kiểm busy **lúc bấm** (`webDashboard.cpp:910`) rồi chốt `OTA_USER_ACCEPTED`; NetworkTask hành
động **vài phút sau** mà không kiểm lại → người dùng bấm Update, đi tới máy, bấm chạy run, rồi
**mất mẫu**. Nay `updateFirmware()`:

- kiểm `dashboardDeviceBusy()` **ngay trước** `httpUpdate.update()` → busy thì `OTA_FAILED`
  (trạng thái nhìn thấy được, không tự thử lại ngầm), không tải, không cướp màn TFT.
- `httpUpdate.rebootOnUpdate(false)` — **bắt buộc**, nếu không thư viện tự `ESP.restart()` bên
  trong (`HTTPUpdate.cpp:353`) và cái check dưới đây không bao giờ chạy.
- thành công → `dashboardRequestRestart()` thay cho `ESP.restart()`. Ảnh đã nằm trong partition
  OTA kia nên chờ được: `dashboardLoop()` reboot khi `!dashboardDeviceBusy()`. Cùng khuôn
  deferred-reboot mà `/otaupload` đã dùng — nay là **một hàm chung**, không phải hai bản chép.
- `_displayCLD.changeScreen = true` trên đường hoãn: `waittingUpdate()` chỉ **vẽ đè** "Waiting..."
  chứ không đổi `type_infor`, nên phải ép vẽ lại kẻo máy đang chạy mà màn hình đứng ở màn OTA.

## Guard: `tools/test_web_assets.py`

```bash
python tools/test_web_assets.py     # exit 0 = pass
```

5 kiểm tra host-side: (1) mọi asset `data/index.html` tham chiếu đều **được nhúng** và **có route**;
(2) `serveStatic` không quay lại; (3) `beginResponse` của asset không có tham số thứ 4 (template
processor); (4) mảng trong `webAssets.h` khớp **từng byte** với `data/` và ETag đúng hash hiện tại;
(5) `.gz` **không cũ** — decompress phải ra đúng file nguồn (generator chỉ so **mtime**, mà mtime
không phải nội dung: checkout/copy/touch là đủ để một `.gz` cũ trông như mới — trước đây nghĩa là
"máy phục vụ UI cũ", **nay nghĩa là UI cũ bị nướng vào `firmware.bin`**).

Kiểm ngược trên bản sao với 4 vector: thêm `theme.css` vào `index.html` mà không nhúng ·
`serveStatic` quay lại · sửa `style.css` mà không sinh lại `.gz` · thả template processor vào
`beginResponse` → guard bắt đủ cả 4.

**Không áp kiểm tra (5) cho `highcharts.js.gz`**: nó ship sẵn bản **minified** (278 589 B sau giải
nén) còn `data/highcharts.js` là bản pretty-print **cùng v11.4.8** (634 985 B) — hai file cố ý
khác nhau, và `serveStatic` xưa cũng phục vụ bản `.gz`, nên **thứ browser nhận không đổi**.

## Chưa làm

- **Gate go/no-go của cả kế hoạch vẫn còn nguyên**: `uxTaskGetStackHighWaterMark(NetworkTask)` sau
  một lần OTA thật (stack bị hạ 8192→6144 mà phải chứa mbedTLS + HTTPClient + Update; overflow là
  **panic**, không phải `HTTP_UPDATE_FAILED`). Cần bench, không đo được ở host.
- Pha 2 (`/slotnames.json` + `/slotsamples.json` sang NVS rồi xoá `LittleFS.begin()`) — vẫn giữ
  LittleFS cho tới lúc đó.
