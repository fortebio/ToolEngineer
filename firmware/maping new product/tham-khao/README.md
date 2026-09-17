# tham-khao/ — hai dự án firmware mã nguồn mở chép **chọn lọc** để học cho Rapid4P

> Chép ngày 2026-09-17, **chỉ đọc, KHÔNG sửa, KHÔNG build tại chỗ** (cây không đầy đủ, cố ý).
> Bài học rút ra + mapping sang `firmware/rapid4p/` viết ở `../THAM-KHAO-xiaozhi-CrossInk.md`.
> Hai dự án đều giấy phép **MIT** → được chép code sang Rapid4P, **phải giữ dòng bản quyền**
> (`LICENSE` từng cây) trong file chép sang.

| Cây | Nguồn | Commit đã chép | Là gì | Chép bao nhiêu |
|---|---|---|---|---|
| `xiaozhi-esp32/` | https://github.com/78/xiaozhi-esp32 | `5d54beb7` (2026-09-16) | Chatbot AI trên ESP32 (ESP-IDF ≥ 6.0.1, 138 board / 171 biến thể, **có ESP32-P4 + MIPI-DSI + ESP-Hosted**) | 1,7 MB / ~19 MB — lớp HAL board, driver dùng chung (AXP2101, backlight, button, power-save), 6 board P4, lớp display/LVGL, application/state machine/OTA/settings, docs, script build ma trận |
| `CrossInk/` | https://github.com/uxjulia/CrossInk | `7a092e82` (2026-09-15), v1.5.1 | Fork CrossPoint Reader — máy đọc sách e-ink ESP32-C3/S3 (PlatformIO + Arduino) | 1,2 MB / ~90 MB — **quy trình + tài liệu kỹ thuật** (AGENTS.md, SCOPE.md, CHANGELOG, docs/), ActivityManager, HAL/capability gating, lib Memory/Serialization/Logging/I18n, script kiểm cỡ firmware + simulator |

## Đã đổi tên để Claude Code / Codex KHÔNG tự nạp

| Gốc | Ở đây | Lý do |
|---|---|---|
| `CrossInk/.claude/` | `CrossInk/_claude/` | Claude Code quét `.claude/skills/` lồng nhau → 6 skill của CrossInk (heap-discipline…) sẽ chui vào danh sách skill của monorepo |
| `CrossInk/CLAUDE.md` | `CrossInk/CLAUDE.md.orig` | Claude Code tự nạp CLAUDE.md theo thư mục khi sửa file bên trong |
| `xiaozhi-esp32/.github/` | `xiaozhi-esp32/_github/` | Chỉ giữ `workflows/build.yml` làm ví dụ ma trận CI, không để GitHub hiểu nhầm |

`AGENTS.md` của cả hai giữ nguyên tên (giống cách giữ `firmware-vimate-p4/AGENTS.md`) — đó chính là
tài liệu đáng đọc nhất.

## Những gì CỐ Ý KHÔNG chép (đọc trên GitHub nếu cần)

- xiaozhi: `main/audio/` (AFE, WakeNet, Opus), `main/protocols/` (WebSocket, MQTT+UDP), `main/mcp_server.*`,
  `main/assets/` (4 MB), `main/led/`, `main/notify/`, 130+ board không phải P4, `docs/v0 v1` (ảnh),
  `scripts/{p3_tools,spiffs_assets,Image_Converter,ogg_converter}`. Rapid4P đã quyết **bỏ hẳn** audio/
  WebSocket/MCP (`../MAPPING-Rapid4P.md` §0).
- CrossInk: `lib/Epub`, `lib/Xtc`, `lib/Txt`, `lib/GfxRenderer`, `lib/EpdFont`, wolfSSL/expat/miniz
  (44 MB), `src/activities/{reader,home,browser,network,settings,…}`, `test/` (2,8 MB), `docs/images`,
  `site/`, `web/`, `freeink-sdk/` (submodule rỗng). Máy đọc sách e-ink không liên quan phần cứng
  Rapid4P — chỉ học **cách tổ chức**.

## Cập nhật lại (khi cần bản mới)

```powershell
# clone nông vào scratchpad rồi chép lại đúng danh sách trên (xem docs/history/2026-09-17-tham-khao-xiaozhi-crossink.md)
git clone --depth 1 https://github.com/78/xiaozhi-esp32.git
git clone --depth 1 https://github.com/uxjulia/CrossInk
```

Sau khi chép, đổi tên lại 3 mục ở bảng trên và cập nhật cột "Commit đã chép".
