# 2026-07-20 — Dọn code chết + tối ưu vẽ LCD (nhóm 1 từ review toàn dự án)

Từ đợt ponytail-review toàn dự án (10 agent). Nhóm 1 = **code chết (rủi ro ~0)** + **2 tối
ưu vẽ LCD**. Mỗi symbol "không caller" đều **tự verify bằng grep trước khi xoá** (thiết bị y
tế — không tin mù findings). Không đụng phần gộp copy-paste / algo / PID (nhóm 2-3, rủi ro cao).

## Code chết đã xoá (đã verify)

| File | Xoá | Ghi chú |
| --- | --- | --- |
| `PIDControl.cpp` | 9 câu `RESPONSE_SIGNAL = RESPONSE_SIGNAL * 1.0;` | no-op (double thường, nhân 1.0) |
| `PIDControl.cpp/.h` | `stopHeaterBottom()` / `stopHeaterTop()` | 0 caller (chỉ `stopAllHeating()` được dùng) |
| `PIDControl.h` | define `OVERHEAT/UNDERHEAT_THRESHOLD_TOP1` + các UNDERHEAT_TOP comment | comment chết |
| `Bluetooth.cpp/.h` | `getData_toChart`+`getCT_toChart`+`getResult_toChart` | cụm khép kín, 0 caller ngoài (giữ "cho endpoint tương lai") |
| `Bluetooth.h` | decl `connectWIFI()` | khai báo suông, không định nghĩa/gọi |
| `displayLCD.cpp` + `displayCLD.h` | `NextTestDisplay()` / `ErrRebootDisplay()` | chỉ còn ở dòng comment trong `loop()` |
| `displayCLD.h` | `reinit()` decl, `instantStatus[2]`, `step`, `temperatureShow` | không ai gọi/đọc |
| `PIDControl.cpp` | nhánh `if (_displayCLD.temperatureShow)` | `temperatureShow` không đâu set true → nhánh dead |
| `sensor6035.h` | `resultData[10]` / `CT_valueData[10]` static | 0 ref (static-in-header → mỗi TU 1 bản thừa) |
| `sensor6035.h` | init demo `sensor67Value = {{1,2,..40}×10}` → `= {}` | GOTCHA 7: "trông như test data"; buffer bị clear đầu mỗi run |
| `sensor6035.cpp` | case `'D'` (top heater 1) comment | ~21 dòng comment chết |
| `updateOTA.cpp` | `http.end()` gọi lần 2 (dòng trong block) | giữ `http.end()` vô điều kiện phía sau |

**Giữ có chủ ý:** `requestReinit` + chord BLUE+WHITE trong button.cpp (tuy `requestReinit`
write-only, nhưng chord còn **xoá `pendingEvent`** — tác dụng thật lên nút, đổi = rủi ro input).
Lớp song ngữ `language` (~85 dòng dead VN) để nhóm 2 (phân tán nhiều hàm).

## Tối ưu vẽ LCD

### #1 — WiFi icon không blit 10 lần/giây nữa (`displayLCD.cpp` `loop()`)

`show_IconWifi()` gọi cuối `loop()` mỗi 100ms cho **mọi màn còn `changeScreen==true`** — kể
cả **suốt run amplification 40 phút** (~24.000 lần blit SPI) dù icon chỉ đổi khi mất/nối WiFi.
Gate lời gọi: chỉ vẽ lại khi **WiFi đổi trạng thái HOẶC vào màn mới** (`type_infor` đổi →
`fillScreen` đã xoá icon). Hàm `show_IconWifi()` giữ nguyên (để `displayWaitingUpData` vẫn vẽ
vô điều kiện). `WiFi.status()` là đọc RAM rẻ, chỉ chặn phần `drawBitmap` đắt.

### #2 — 5 màn tĩnh vẽ 1 lần, không repaint toàn màn mỗi 10s

`waitLysisTube`, `waitBtnStartPhase2`, `waitAmpTube`, `waitAmpSetName`, `prepare` trước đây
`fillScreen(BLACK)` + vẽ lại dải hazard (~60 primitive) **mỗi 10 giây** dù nội dung không đổi
→ **nháy đen định kỳ**. Đã thay guard `if (timeRefresh > now) ... timeRefresh = now+10s` bằng
**`changeScreen = false`** (draw-once/entry, như các màn one-shot khác).

**An toàn đã kiểm:** cả 5 màn **thuần tĩnh** (không buzzer/timer per-tick); **8 transition vào
5 state đều set `changeScreen=true` và one-shot** (2 chỗ có `BuzzerAlert` xác nhận, các chỗ
khác chuyển `pidStep`/`sensorStep`/guard `== escreenStart`) → vào màn vẫn vẽ đúng 1 lần; ra
màn qua nút vẫn set `changeScreen=true`. Không đổi thư viện, không framebuffer (hết RAM).

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS** (RAM 22.8%, **Flash 2.301.581 B, −2.2KB so với gốc**).
- Mỗi lần xoá "không caller" verify bằng `grep -rIn "\bsym\b" src/` (lọc comment) trước khi động.

## Nạp

Đổi `src/` (+ `sensor6035.h`) → `pio run -e esp32dev -t upload --upload-port COMxx`. `data/`
không đổi trong nhóm này. **LCD #1/#2 nên xem mắt trên TFT sau nạp** (icon WiFi còn hiện; 5 màn
chờ vẽ đúng, hết nháy).
