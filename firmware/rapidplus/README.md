# FBT x Dxd

Firmware ESP32 (PlatformIO / Arduino) cho máy **FBT RAPID** — xét nghiệm LAMP-PCR.
Đo opto khuếch đại (VEML6035), điều khiển nhiệt (PID heater + hotlid), màn TFT,
3 nút, upload kết quả lên **Google Sheet + ingest + ERP cloud** (TLS), và **web dashboard**
phục vụ từ thiết bị.

**Kiến trúc chi tiết (có sơ đồ):** [docs/architecture/](docs/architecture/). Hướng dẫn cho lập trình viên: [CLAUDE.md](CLAUDE.md). Nhật ký thay đổi: [docs/history/](docs/history/). Spec giao diện: [docs/GUI_SSE/GUI.md](docs/GUI_SSE/GUI.md).

## Web dashboard (SSE)

Dashboard phục vụ ngay từ thiết bị, đẩy dữ liệu realtime qua Server-Sent Events.

- File UI: `data/` (`index.html`, `script.js`, `style.css`, `highcharts.js`) — phục
  vụ từ LittleFS, không cần build.
- Màn hình: **Home** (nhiệt độ, trạng thái, thông báo, nút điều khiển GREEN/RED/WHITE,
  **chart live** khi đang chạy), **Result** (xem lại: bảng kết quả + chart đã lưu),
  **Setting**.
- **Điều khiển từ xa:** bấm nút trên web (`/control?btn=green|red|white`) = bấm nút
  vật lý trên máy.
- **Đặt tên slot trước khi chạy:** vào pha khuếch đại (preheat xong / skip preheat),
  Home hiện bảng đặt tên slot; nút **Start** bị khoá tới khi bấm **Confirm** (ô trống →
  mặc định #1–#10). Sau confirm: Start mở, chart live hiện, nhiệt độ thu về 1 strip nhỏ.
  Khoá chỉ phía web — **nút vật lý trên máy vẫn chạy**.
- **Cài đặt từ web (tab Setting):** 7 card — WiFi (tự dò mạng), Device ID, Test profile
  (nhiệt/thời gian lysis + amp), LED, Calib (wizard chạy quy trình trên máy), PID/heater,
  Other (tham số thuật toán). **Khoá toàn bộ khi máy đang chạy** (chặn cả phía server).
- **Chart vẽ lại từ đầu run:** thiết bị giữ toàn bộ đường cong; mở web muộn hoặc mất
  kết nối giữa chừng → client gọi `/curve` và backfill lại đủ, không mất đoạn nào.
- **Xem lại run cũ sau khi tắt/bật máy:** run cuối lưu trong EEPROM. Mở tab Result sau
  reboot → client tự `POST /reviewlast`, thiết bị nạp lại và tính lại từ EEPROM → bảng
  kết quả + chart hiện lại (máy mới tinh thì trống, không rác).
- **Chạy offline:** nếu không có WiFi, thiết bị tự phát SoftAP `RAPID-<id>` → nối
  điện thoại vào, mở `http://192.168.4.1/`. Highcharts nhúng nội bộ nên chart chạy
  cả khi không có internet.

Truy cập: `http://<IP-thiết-bị>/` (IP in ra Serial: `[dash] dashboard on http://...`).

### Test không cần phần cứng

```bash
python tools/sse_test_server.py        # rồi mở http://localhost:8000
python tools/sse_test_server.py selftest
```

Mock ESP32 (Python stdlib) phục vụ chính file `data/` + stream SSE giả (vòng đời
heating → amplification → finished) để kiểm UI trên trình duyệt trước khi nạp.

## Build / nạp (PlatformIO)

```bash
pio run -e esp32dev              # build firmware
pio run -e esp32dev -t upload    # nạp firmware (khi đổi code src/)
pio run -e esp32dev -t uploadfs  # nạp data/ vào LittleFS (khi đổi file UI)
pio device monitor -b 115200     # xem Serial
pio test -e esp32dev_test -v     # unit test on-device
```

Đổi code `src/` → `upload`. Đổi file `data/` → `uploadfs`. Đổi cả hai → chạy cả hai.

## Quy ước

- **Code / UI / comment: tiếng Anh** (an toàn font trên thiết bị). **Tài liệu (.md,
  gồm README/CLAUDE/history): tiếng Việt.**
- Mỗi thay đổi liên quan: cập nhật `README.md` + `CLAUDE.md` và thêm 1 file có ngày
  trong `docs/history/`.
