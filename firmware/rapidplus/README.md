# FBT RAPID — firmware v2.4.3

Firmware ESP32 (PlatformIO / Arduino) cho máy **FBT RAPID** — xét nghiệm LAMP-PCR.
Đo opto khuếch đại (VEML6035), điều khiển nhiệt (PID heater + hotlid), màn TFT 3 nút,
đẩy kết quả lên **Google Sheet + ingest + ERP** (TLS), kèm **web dashboard** phục vụ ngay
từ thiết bị.

**Toàn bộ giao diện web nằm trong `firmware.bin`.** Máy **không mount filesystem nào** —
không LittleFS, không `uploadfs`. Một máy chỉ cần **một artifact duy nhất**, và đó là điều
khiến cập nhật qua mạng trở nên an toàn (đường ghi `spiffs` của ESP32 **không có kiểm tra
toàn vẹn nào**: bất kỳ HTTP 200 nào cũng được ghi thẳng lên partition).

| Đo thật (build sạch + board `RPL00001`) | |
| --- | --- |
| Flash | **70.7%** — 2 364 541 B / app slot 3 342 336 B |
| RAM tĩnh | 77 036 B (23.5%) |
| Khối heap liền mạch lúc rảnh | ~86 000 B (mbedTLS cần ~42 000) |

Nhúng 147 872 B giao diện vào firmware **gần như không tốn thêm flash**: cả stack
LittleFS/VFS bị gỡ cùng lúc đã trả lại xấp xỉ đúng chừng ấy.

**Tài liệu:** [CLAUDE.md](CLAUDE.md) (hướng dẫn cho người sửa code — đọc trước khi động vào)
· [docs/architecture/](docs/architecture/) (sơ đồ) · [docs/history/](docs/history/) (nhật ký
từng thay đổi) · [docs/GUI_SSE/GUI.md](docs/GUI_SSE/GUI.md) (spec giao diện).

---

## Bắt đầu

```bash
cp src/secrets.example.h src/secrets.h   # rồi ĐIỀN giá trị thật
pio run -e esp32dev                      # build
pio run -e esp32dev -t upload            # nạp firmware
pio device monitor -b 115200             # xem Serial
```

⚠ **`src/secrets.h` là bắt buộc và bị gitignore.** Thiếu nó build **fail có chủ đích** — thà
lỗi biên dịch còn hơn âm thầm ship placeholder. File chứa 5 macro: GAS URL, ingest URL +
Bearer, ERP URL + X-API-Key.

Đổi code trong `src/` **hoặc** file giao diện trong `data/` → **chỉ cần `upload`**.
`tools/pio_gzip_data.py` tự nén `data/` và sinh `src/webAssets.h` mỗi lần build; `uploadfs`
không còn liên quan gì tới giao diện nữa.

**Truy cập dashboard:** `http://<device-id>.local/` (mDNS, chữ thường — VD `RPL00001` →
`http://rpl00001.local/`) hoặc `http://<IP>/` (IP in ra Serial: `[dash] dashboard on ...`).
Chỉ trong cùng LAN. **Chrome trên Android không phân giải `.local`** — dùng QR hoặc IP.

---

## Web dashboard (SSE)

Ba màn: **Home** (nhiệt độ, trạng thái, nút điều khiển, chart live khi đang chạy) ·
**Result** (bảng kết quả + chart đã lưu) · **Setting** (8 card).

- **Điều khiển từ xa** — bấm nút trên web (`/control?btn=green|red|white`) tương đương bấm
  nút vật lý.
- **Đặt tên slot trước khi chạy** — chọn bệnh từ danh sách cố định `{PC, EHP, EMS, WSSV, TPD}`
  kèm ô tên mẫu tự do; **Start khoá** tới khi bấm Confirm. Khoá chỉ ở phía web — **nút vật lý
  vẫn chạy**. Nhãn lưu trong **NVS** nên sống qua mọi lần nạp firmware.
- **Chart vẽ lại từ đầu run** — thiết bị giữ toàn bộ đường cong; mở web muộn hay mất kết nối
  giữa chừng, client gọi `/curve` và backfill đủ, không mất đoạn nào.
- **Xem lại run cũ sau khi tắt/bật máy** — run cuối nằm trong EEPROM; mở tab Result là client
  tự `POST /reviewlast?go=1`, thiết bị nạp lại và tính lại (~8 giây).
- **Cài đặt từ web** — WiFi (dò mạng, lưu nhiều mạng), Device ID, test profile, LED, calib
  wizard, PID/heater, tham số thuật toán, firmware. **Khoá toàn bộ khi máy đang chạy**, chặn
  ở phía server chứ không chỉ phía client. Từ 2026-07-29 đây là **đường duy nhất** để nhập
  WiFi — captive portal WiFiManager trên TFT đã bị gỡ (xem
  [nhật ký](docs/history/2026-07-29-remove-wifimanager.md); **đừng thêm lại**).
- **QR trên TFT** — nút TRẮNG ở màn chính hiện QR dẫn thẳng vào dashboard.
- **Chạy offline** — không có WiFi thì máy tự phát SoftAP `RAPID-<id>` (`192.168.4.1`) kèm
  captive portal. Highcharts nhúng sẵn nên chart chạy cả khi không internet.
- **Giới hạn 2 người xem cùng lúc** (`MAX_VIEWERS`). Người thứ 3 vẫn xem được trang, chỉ
  không có luồng realtime.

---

## Cập nhật firmware

**Từ trình duyệt** (máy không có internet, hoặc build chưa publish):

```bash
curl -F "firmware=@.pio/build/esp32dev/firmware.bin" \
     "http://<ip>/otaupload?md5=$(md5sum .pio/build/esp32dev/firmware.bin | cut -d' ' -f1)"
```

`?md5=` **tuỳ chọn nhưng nên có**: không có nó thì **một file `.bin` đứt giữa chừng vẫn boot
được** — đây là đường brick thật duy nhất của hệ thống. Nhận cả chữ hoa (PowerShell
`Get-FileHash`). Máy tự reboot sau khi nạp xong, và **hoãn reboot nếu đang chạy run**.

**Từ Engineer Server (OTA)** — từ **v2.4.4** máy hỏi `GET /ota/check?device=<id>` (Bearer =
`SECRET_INGEST_TOKEN`); admin chọn bản trong tab "Quản lý máy" của app. Thử **hai host theo thứ
tự**: `hub.fortebio.tech` (Cloudflare) rồi `fbt.basa-luma.ts.net` (Funnel, lưới an toàn — OTA là
đường duy nhất tới máy ngoài hiện trường). Server trả **tên file**, firmware so với `FirmwareVer`
→ **đặt tên `fbt_v<version>.bin` là bắt buộc**. Chi tiết + vì sao không tự viết vòng tải:
[docs/history/2026-08-17-ota-github-to-server.md](docs/history/2026-08-17-ota-github-to-server.md).
Đường GitHub cũ (`tools/ota-release/`, `baseUrl` theo branch version) **đã bỏ** — giữ thư mục đó
làm hồ sơ các đợt phát hành trước, đừng dùng lại.

---

## Kiểm thử

**Không cần phần cứng** — mock ESP32 viết bằng Python stdlib, phục vụ `data/` + giả lập SSE
theo đúng vòng đời máy (heating → amplification → finished) và **chờ đúng chỗ máy thật chờ**.

```bash
python tools/sse_test_server.py            # mock -> http://localhost:8000
python tools/sse_test_server.py --full     # scale THẬT: 120 vòng x 20s, nén đồng hồ còn ~20s
python tools/sse_test_server.py --reboot   # giả cảnh vừa tắt/bật: run cũ trong EEPROM, RAM trống
python tools/sse_test_server.py selftest   # tự kiểm các hàm thuần
```

**Guard tĩnh** (chạy nhanh, nên chạy trước mỗi commit):

```bash
python tools/test_phase0_guards.py          # không strcpy vào parameter.*, 4 field char[10] được validate, secrets sạch
python tools/test_web_assets.py             # UI nhúng đủ + có route + .gz không cũ + không serveStatic + không LittleFS
python tools/test_ota_guards.py             # OTA -> server (Bearer, so tên file), UI nạp gửi ?md5=, eUpdateOTA không "busy"
node tools/test_ota_md5.js                  # md5Hex() trong script.js là MD5 ĐÚNG (RFC 1321 + firmware.bin thật)
python tools/test_no_method_branch.py       # không handler nào so req->method()  (số học GOTCHA 3)
python tools/test_no_runtime_wifi_begin.py  # WiFi.begin() chỉ ở setup()
g++ -O2 -std=c++17 tools/test_readcmd_overflow.cpp -o t && ./t   # readCommand không tràn buffer
g++ -O2 -std=c++17 tools/test_wifi_store.cpp     -o t && ./t     # contract danh sách WiFi đã lưu
g++ -O2 -std=c++17 tools/test_curve_length.cpp   -o t && ./t     # /reviewlast quét đúng độ dài run
```

**E2E qua trình duyệt** (Edge headless + DevTools Protocol, chỉ dùng node stdlib):

```bash
node tools/test_review_reboot.js   # tự bật mock
node tools/test_wifi_e2e.js        # tự bật mock
python tools/sse_test_server.py --full &   # hai bài dưới CẦN mock chạy sẵn
node tools/test_full_run.js        # đi hết một run 120 vòng: heating -> đặt tên -> Start -> finished -> Result
node tools/test_chart_ticks.js     # trục Y luôn đúng 10 nấc, sàn 200
node tools/ui_screenshot.js <thư-mục>   # chụp 9 trạng thái UI để soát thiết kế
```

**Trên máy thật:**

```bash
pio test -e esp32dev_test -v                        # tất cả
pio test -e esp32dev_test -f test_endrun_upload -v  # khâu cuối: run 40' + dashboard + upload
```

⚠ `pio test` **nạp firmware test đè lên máy** → nhớ nạp lại firmware thật sau khi test.

---

## Kiến trúc RTOS

`setup()` nối WiFi rồi tạo 6 task, `loop()` để trống:

| Task | Core | Prio | Việc |
| --- | --- | --- | --- |
| ControlTask | 1 | 5 | PID + quạt, mỗi 100 ms |
| SensorTask | 1 | 2 | đọc VEML6035, mỗi 20 ms (mutex I2C) |
| DisplayTask | 0 | 2 | TFT, mỗi 100 ms — **và TLS upload chạy trên stack này** |
| NetworkTask | 0 | 1 | OTA + dashboard loop, mỗi 10 ms |
| InputTask | 1 | 3 | 3 nút + buzzer, mỗi 5 ms |
| SettingTask | 0 | 1 | cấu hình qua Serial/web, mỗi 10 ms — **mọi ghi EEPROM dồn về đây** |

Chi tiết + sơ đồ: [docs/architecture/](docs/architecture/).

---

## Quy ước

- **Code / UI / comment: tiếng Anh** (an toàn font trên thiết bị).
  **Tài liệu `.md` (README, CLAUDE, history): tiếng Việt.**
- Mỗi thay đổi: cập nhật `README.md` + `CLAUDE.md` và thêm **1 file có ngày** trong
  `docs/history/` giải thích *tại sao*, không chỉ *cái gì*.
- Đọc [CLAUDE.md](CLAUDE.md) mục **GOTCHAS** trước khi sửa phần mạng, heap hay hiển thị.
  Phần lớn là những cái bẫy đã làm chết máy ngoài thực địa một lần rồi.
