> **Cay firmware nay build cho ESP32-P4** (board FBT + LCD 4.3" ST7102 MIPI-DSI).
> File README nay la ban sao tu cay S3 va van mo ta phan cung S3.
> **Doc [README-P4.md](README-P4.md) truoc** — do la tai lieu dung cho cay nay.

# firmware-vimate

Native ESP-IDF firmware cho **VIMATE** — thiết bị AI đào tạo trẻ em.

> Mục tiêu: thoát hoàn toàn khỏi codebase Xiaozhi để dùng thương mại,
> chạy native ESP-IDF ≥ v5.2 trên ESP32-S3 + LCD 2.8" (ILI9341/ST7789).

## Tính năng

| Mục | Mô tả |
|---|---|
| WiFi STA + AP provisioning | Quên SSID → auto bật AP `VIMATE-Setup-XXXX`, portal HTTP nhập WiFi. |
| OTA HTTPS có rollback | Dual partition `ota_0` + `ota_1`, verify SHA256 + ESP32 magic byte trên server. |
| WebSocket Authorization Bearer | Reconnect exponential 5s→2min. |
| Opus voice pipeline | Mic 16kHz upstream, speaker 24kHz downstream, frame 60 ms. |
| LVGL UI | 21-emotion sprite/emoji full-screen + lesson card + reward stars + lesson image. |
| MCP tool calls | `self.edu.show_image / show_card / show_reward` từ server. |
| Asset pack premium | Download manifest từ `/api/devices/:macId/asset-pack` → SPIFFS. |
| Course media cache | Download media của khoá học đã mua từ `/api/devices/:macId/course-cache` → thẻ SD; fallback HTTP khi thiếu cache. |
| Lesson image fallback | Sync ảnh bài học nhỏ từ `/api/devices/:macId/lesson-images` → SPIFFS để boot an toàn khi SD chưa mount. |
| Activation 6-char code | Hiện lớn cho phụ huynh nhập vào vimate.vn. |
| Long-press WiFi setup | Hold BOOT button 5s → clear saved WiFi, reboot, force AP `VIMATE-Setup-XXXX` at `192.168.4.1`. |

## Hardware target

- **MCU**: ESP32-S3 N16R8 (16MB QIO flash, 8MB OCT PSRAM)
- **LCD**: 2.8" 320×240 SPI ILI9341 hoặc ST7789
- **Audio codec**: ES8311 I2C + I2S full-duplex (mic + speaker amp)
- **Button**: BOOT (GPIO0)
- **Storage**: thẻ SD mặc định cho media khoá học đã mua
- Chi tiết pinout: `main/boards/board_esp32s3_28lcd.h`

## Build

```bash
cd firmware-vimate
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py menuconfig          # tuỳ chỉnh CONFIG_VIMATE_*
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

ESP-IDF tự kéo các managed components (LVGL 9, esp_lvgl_port, esp_lcd_ili9341,
esp_codec_dev, opus) qua `main/idf_component.yml`.

## Production / soak

```bash
# Lab soak build + overnight monitor
scripts/build_soak.sh
scripts/soak_monitor.sh /dev/cu.usbmodemXXXX

# Hardened production build profile
scripts/prepare_production_keys.sh
scripts/build_production.sh
```

Secure Boot/Flash Encryption là thao tác eFuse một chiều. Đọc
`docs/PRODUCTION_CHECKLIST.md` trước khi flash profile production lên máy thật.

## Cấu hình runtime

`menuconfig → VIMATE`:
- `VIMATE_SERVER_BASE` — default `https://vimate.vn`
- `VIMATE_OTA_INTERVAL_SEC` — chu kỳ check OTA (default 6h)
- `VIMATE_WS_RECONNECT_BASE_MS` / `_MAX_MS` — exponential backoff
- `VIMATE_SD_CACHE_ENABLE` — bật cache media khoá học xuống thẻ SD
- `VIMATE_SD_SPI_MOSI/MISO/SCLK/CS` — chân SD SPI, cần cấu hình theo board thật
- `VIMATE_DIAG_ENABLE` — log reset reason + heap telemetry cho soak/debug

## Cấu trúc source

```
main/
├── app_main.c           # bootstrap orchestrator
├── vimate.[ch]          # global types, emotion enum, state machine
├── core/                # wifi_mgr, nvs_store, system_info
├── network/             # ota_client, ws_client, http_dl
├── protocol/            # envelope (JSON), mcp_handler (JSON-RPC)
├── audio/               # i2s_input/output, opus_codec, audio_pipeline
├── ui/                  # display + LVGL screens (home/emotion/activation/etc.)
├── store/               # asset_pack, lesson_image_cache, course_media_cache
├── input/               # button (GPIO0)
└── boards/              # board pinout (ESP32-S3 + 2.8" LCD)
```

## Partition layout (`partitions/partitions.csv`)

```
nvs           24 KB
otadata        8 KB
ota_0          4 MB    # app slot A
ota_1          4 MB    # app slot B (OTA target)
asset_spiffs   3 MB    # premium asset pack (emotion sprites + sfx)
lesson_spiffs  3 MB    # lesson-image fallback cache
emo_spiffs   512 KB    # bundled/default emotion GIFs
```

## Server protocol

Tham chiếu: `server/docs/FIRMWARE_PROTOCOL.md`.

## License

Internal — VIMATE 2025.
