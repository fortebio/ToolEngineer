# 2026-09-17 — Bổ sung tài liệu học tập cho Rapid4P: xiaozhi-esp32 + CrossInk (chép chọn lọc)

Phần: `firmware/maping new product/` (tài liệu khảo sát) + pointer trong `firmware/rapid4p/CLAUDE.md`
và `CLAUDE.md` gốc → ghi ở gốc.

## Vì sao
Rapid4P đã nạp máy thật (xem `2026-09-17-rapid4p-nap-may-that-lan-dau.md`) nhưng còn nhiều khối chưa có
tiền lệ trong repo: driver PMIC AXP2101 (% pin / tắt máy), ngủ → tắt máy, 4G ML307, so phiên bản OTA,
simulator UI trên PC, quy trình tài liệu cho một cây firmware. Hai dự án mã nguồn mở này đã giải đúng
các bài đó, và **xiaozhi-esp32 chạy trên cùng ESP32-P4 + MIPI-DSI + ESP-Hosted** như board Rapid4P.

## Đã làm
- Clone nông (`--depth 1`) hai repo vào scratchpad, **chép chọn lọc** (3,0 MB / ~110 MB, 294 file) vào
  `firmware/maping new product/tham-khao/{xiaozhi-esp32,CrossInk}/`:
  - xiaozhi `5d54beb7` (2026-09-16, MIT): `AGENTS.md`, `docs/*.md`, `sdkconfig.defaults*`, `partitions/`,
    `main/{application,device_state_machine,ota,settings,system_info,main}.*`, `main/boards/common/` (AXP2101,
    backlight, button, power_save_timer, wifi/ml307/dual_network board…), 6 board P4 (Waveshare ×3,
    ESP32-P4-Function-EV, M5Stack Tab5 + CoreP4) + 2 board có khởi tạo AXP2101 đầy đủ (kevin/box-2,
    m5stack/core-s3), `main/display/` (LVGL theme/font/glyph cache), `scripts/build.py` + `tests/`,
    `_github/workflows/build.yml`.
  - CrossInk `7a092e82` (2026-09-15, v1.5.1, MIT): `AGENTS.md`, `SCOPE.md`, `CHANGELOG.md`, `docs/`
    (không ảnh), `_claude/` (CONTEXT.md + 6 skill), `src/{main,CrossPointSettings,CrossPointState,
    MappedInputManager,QuickActions}.*`, `src/activities/{Activity,ActivityManager,RenderLock…}`,
    `lib/{Memory,MemoryBudget,Logging,Serialization,hal,I18n(en+vi)}`, `include/*Capabilities.h`,
    6 script (kiểm cỡ firmware, gen_i18n, smoke test simulator…), `platformio.ini`.
  - Không chép: audio/protocols/assets/130 board khác (xiaozhi); EPUB/e-ink/wolfSSL/test (CrossInk).
    Danh sách đầy đủ + cách cập nhật: `tham-khao/README.md`.
- Đổi tên để công cụ không tự nạp: `CrossInk/.claude → _claude`, `CrossInk/CLAUDE.md → CLAUDE.md.orig`,
  `xiaozhi-esp32/.github → _github`. Kiểm `git check-ignore`: không file nào bị `.gitignore` nuốt.
- Viết `firmware/maping new product/THAM-KHAO-xiaozhi-CrossInk.md`: 12 điểm từ xiaozhi (AXP2101 reg
  map, component `esp_lcd_touch_st7123` chính thức, cờ P4 rev < 3 — Rapid4P đã có, lớp Board + luật
  OTA theo `hw`, state machine có kiểm chuyển, PowerSaveTimer, backlight fade, iot_button, OTA semver
  + `mark_app_valid` + server_time, Settings dirty-commit, ML307 4G, ma trận build) + 8 điểm từ CrossInk
  (AGENTS.md mẫu, SCOPE.md, CHANGELOG/CONTEXT, kỷ luật heap, ActivityManager, capability gating,
  simulator PC, kiểm cỡ firmware/I18n) + bảng **12 việc đề xuất theo thứ tự** + lưu ý giấy phép.
- Sự thật Rapid4P đã đối chiếu khi viết: `ota_client.c:93` nâng cấp khi `strcmp != 0` (hạ cấp cũng nạp);
  `nvs_store.c` mỗi `set_*` là một chu kỳ open/commit/close; `sdkconfig.defaults` đã có
  `SELECTS_REV_LESS_V3`, `REV_MIN_100`, `ESP_HOSTED_MEMPOOL_PREFER_SPIRAM`, cố ý không bật
  `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`.
- `CLAUDE.md` gốc (hàng `firmware/maping new product/`) và `firmware/rapid4p/CLAUDE.md` (đầu file)
  thêm pointer tới `THAM-KHAO-…md` và `tham-khao/`.

## Chưa làm / lưu ý
- Chưa sửa gì trong `firmware/rapid4p/` ngoài CLAUDE.md; 12 việc đề xuất nằm ở §3 tài liệu, việc #1
  (AXP2101) và #3 (LVGL simulator) là hai việc lợi nhất.
- xiaozhi bản này yêu cầu **ESP-IDF ≥ 6.0.1**; Rapid4P đang 5.5.4 → code chép sang phải kiểm API
  (i2c_master, esp_lcd DSI giống nhau; `std::expected` trong `ota.h` là C++23, không dùng cho C).
