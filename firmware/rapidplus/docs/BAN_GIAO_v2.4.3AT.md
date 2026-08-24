# Bàn giao v2.4.3AT

Đọc file này TRƯỚC khi merge. Mục tiêu: hiểu 20 file đã đổi trong `7243cff` mà không phải
đọc hết 2 242 dòng diff.

| | |
| --- | --- |
| Branch | `v2.4.3AT` |
| Commit gốc của thay đổi | `7243cff` (17/08/2026) |
| Nền | `8b43397` (`v2.4.3`, bản của Kane) |
| Bản `.bin` team đang test | `RAPIDPlus_v2.4.3AT_firmware.bin`, sha `C60F1479…` |
| Env build | `esp32dev` → FirmwareVer `v2.4.3AT` |

**Xin lỗi vì cách bàn giao lần trước.** `7243cff` gộp 3 ngày làm việc vào một commit nên
không tách được phần di trú EEPROM khỏi phần thuật toán, không bisect được. Các commit sau
đó trên branch này đi theo đúng quy tắc trong `CLAUDE.md`: mỗi commit một việc.

---

## 1. Đổi gì mà người dùng THẤY được

Ba thứ, không hơn:

1. **Run rút từ 40 phút xuống 30 phút** (120 vòng → 90 vòng, mỗi vòng vẫn 20 s).
2. **Ngưỡng chặt hơn**: `min_increase` 20 → 25, `min_sharpness` 5 → 8.
3. **Có thêm CHỮ KẾT QUẢ THỨ SÁU: `F` (Flagged)** — bên cạnh P/N/S/E/B. Nghĩa là *"có
   khuếch đại, nhưng hình dạng đường cong không giống phản ứng thật"*. **`F` là một trạng
   thái riêng, không phải Positive kèm ghi chú.** Máy đang nói với người dùng: tôi không
   dám đứng sau kết quả này.

⚠️ **Mọi thứ đọc chữ kết quả đều phải biết chữ `F`**: bảng trên TFT, `GET /slots`, payload
upload, Google Sheet, ERP, email báo kết quả. Chuỗi `if` trong `displayLCD` **không có
nhánh else** — chữ lạ sẽ in ra ô TRỐNG chứ không báo lỗi. Đây là chỗ dễ mất dữ liệu nhất
khi merge.

---

## 2. Đọc theo thứ tự này (4 tài liệu đã có sẵn trong repo)

| Thứ tự | File | Trả lời câu hỏi |
| --- | --- | --- |
| 1 | [docs/history/2026-08-13-thuat-toan-goi-ket-qua-v2.4.3a.md](history/2026-08-13-thuat-toan-goi-ket-qua-v2.4.3a.md) | Thuật toán gọi kết quả hoạt động thế nào (baseline, làm mượt, Ct, increase, sharpness) |
| 2 | [docs/history/2026-08-15-quy-tac-hinh-dang-va-tach-nguong-jump.md](history/2026-08-15-quy-tac-hinh-dang-va-tach-nguong-jump.md) | Quy tắc hình dạng ra đời từ đâu, chữ `F` từ đâu ra |
| 3 | [docs/history/2026-08-16-min-sharpness-8-tail-climb-window-rate.md](history/2026-08-16-min-sharpness-8-tail-climb-window-rate.md) | Vì sao ngưỡng là 8.0 chứ không phải 11.0, và TAIL climb repair |
| 4 | [docs/history/2026-08-15-di-tru-cau-hinh-va-tu-kiem-tra.md](history/2026-08-15-di-tru-cau-hinh-va-tu-kiem-tra.md) | Vì sao phải di trú EEPROM, và cách tự kiểm tra một máy |
| ⚠️ | [docs/history/2026-08-21-asf-va-quy-tac-hinh-dang.md](history/2026-08-21-asf-va-quy-tac-hinh-dang.md) | **Bằng chứng KHÔNG được cài bản này lên máy chạy ASF** |

---

## 3. Bản đồ 20 file

**Thuật toán (đây là phần chính, 703 dòng):**

| File | Đổi gì | Giải thích ở |
| --- | --- | --- |
| `src/Alg/Algo.cpp` (+509) | Climb repair có thêm loại TAIL; bỏ `amplification_share`, thay bằng `window_rate`; quy tắc hình dạng | doc 1, 2, 3 |
| `src/Alg/Algo.h` (+143) | Các hằng số: `CLIMB_*`, `NSA_*`, `SHAPE_DERIV_FRACTION`, `WINDOW_RATE_MIN`, `LEGACY_MIN_*` | doc 2, 3 |
| `src/Alg/AlgoData.h` (+51) | `OutcomeFlagged` — chữ `F`; `suspect_score` và `arm_width` chỉ để tham khảo, KHÔNG đổi kết quả | doc 2 |

**Cấu hình / di trú (phần dễ mất nhất khi merge):**

| File | Đổi gì | Giải thích ở |
| --- | --- | --- |
| `src/define.h` (+141) | 120 → 90 vòng; ngưỡng mới; `ADDR_CONFIG_REV 236`, `CONFIG_REV_THRESHOLDS 2` | doc 4 |
| `src/ForteSetting.cpp` (+234) | Di trú một lần: ghi timing + ngưỡng mới vào EEPROM, **giữ nguyên slopes/origins/LED** và bỏ toàn bộ di trú nếu hiệu chuẩn lệch | doc 4 |
| `src/ForteSetting.h` (+13) | `configSelfCheckJson()` | doc 4 |
| `tools/test_config_migration.py` (+229) | Guard cho toàn bộ phần trên, 7 nhóm kiểm tra | doc 4 |

**Vì sao phải di trú, nói ngắn:** `ForteSetting::begin()` ghi đè tham số biên dịch bằng bản
trong EEPROM mỗi khi `sizeof(parastructure)` không đổi — và nó không đổi từ v2.4.2. Nên
**sửa giá trị mặc định trong `define.h` KHÔNG tới được bất kỳ máy nào đã từng cấu hình.**
Máy vẫn chạy 120 vòng / 20 / 5, và vì `removed_by_new_gate()` so ngưỡng chạy thật với
`LEGACY_MIN_*`, hai vế bằng nhau nên **cổng review không bao giờ kích hoạt** — cả hai bản
build sẽ chạy im lặng với tính năng chết. `CONFIG_REV_THRESHOLDS` là 2 chứ không phải 1 vì
rev 1 đã lên RPL03001 và RPL03002 ngày 15/08.

**Web + màn hình:**

| File | Đổi gì |
| --- | --- |
| `data/script.js` (+55) | Hiển thị chữ `F`, badge tím, tooltip giải thích |
| `data/style.css` (+15) | `.res-F` — badge thứ sáu, tương phản 5.06:1 |
| `src/sensor6035.cpp` (+59) | Nơi kết quả thành chữ; chữ này đi thẳng lên TFT, `/slots` và payload upload |
| `src/webDashboard.cpp` (+47) / `.h` (+7) | `GET /selfcheck`; cache số đo hình dạng cho `GET /slots` |

**Hạ tầng:**

| File | Đổi gì |
| --- | --- |
| `platformio.ini` (+18) | Thêm env `esp32dev_shape_neg` (= v2.4.3a: well bị flag → gọi Negative). Cùng một source, khác một cờ |
| `.gitignore` / `src/secrets.h` (−14) | `secrets.h` bị bỏ tracking; token thật đã lộ trong `8b43397` |
| `src/Bluetooth.cpp` (+4) | Lấy endpoint/token từ macro `SECRET_*` |

---

## 4. Cách kiểm tra (không cần máy)

```bash
cp src/secrets.example.h src/secrets.h     # thiếu file này thì build fail
python tools/check.py                      # phải xanh trước khi merge
pio run -e esp32dev                        # bản trial: F được báo, kết quả giữ nguyên
pio run -e esp32dev_shape_neg              # bản v2.4.3a: F bị gọi thành Negative
```

Hiện trên branch này: **10 pass, 0 fail, 5 skip** (skip vì máy không có `g++`), 1 guard
được ghi trong CLAUDE.md nhưng chưa tồn tại (`test_upload_targets.py`).

## 5. Cách kiểm tra một MÁY THẬT đã di trú chưa

```
GET /selfcheck
```

Trả về thời gian lysis và thời gian gọi kết quả **tính bằng phút**, dùng `timePerLoop` của
chính máy đó, kèm trạng thái cổng review là `LIVE` hay `INERT`.

> **Máy nào báo 40 phút là CHƯA di trú.** Máy đã di trú báo 30 phút và `LIVE`.

Trên Serial lúc boot lần đầu sau khi nạp:

```
[cfg] migrated to rev 2: lysis 10 min, calling 30 min, min_increase 25.0,
      min_sharpness 8.0 - slopes/origins/LED power untouched
```

---

## 6. Những điều KHÔNG được làm

1. **KHÔNG cài v2.4.3AT lên máy chạy ASF.** Trên đĩa ASF có qPCR đối chứng, 10/10 mẫu là
   dương thật và bản này gắn cờ 8 mẫu. `min_sharpness = 8.0` chỉ đúng với LAMP tôm
   (dương chạy 20–60); ASF chạy 4.0–8.4. Chi tiết + số liệu: doc ASF ở mục 2.
2. **KHÔNG merge branch `v2.4.3AT-with-zh`.** Đó là bản tiếng Trung, cố ý không đưa vào
   branch này; build từ nó ra file `.bin` KHÁC với bản team đang test.
3. **KHÔNG publish v2.4.3AT lên OTA.** Không có branch nào trên `FBTRapidplusOTA` cho
   version này, và cố ý như vậy cho tới khi ngưỡng hình dạng biết phân biệt theo bệnh.
   Nạp tay qua `POST /otaupload?md5=<32 hex>` — **luôn kèm `?md5=`**, thiếu nó thì một
   file upload bị cắt dở vẫn boot.
4. **KHÔNG bỏ `-Wformat` khỏi `build_flags`.** Nó chặn đúng loại lỗi đã làm reset một máy
   ngoài hiện trường.

## 7. Còn nợ (chưa làm, cần biết trước khi merge)

- **Token đã lộ trong `8b43397` vẫn cần rotate phía server.** Bỏ file khỏi tracking không
  xoá nó khỏi lịch sử git — ai clone repo vẫn đọc được.
- `test_upload_targets.py` được ghi trong CLAUDE.md nhưng không tồn tại.
- OTA của `v2.4.3` đang hỏng: manifest trỏ tới `firmware.bin` trả về 404; `v2.4.2` tự khai
  là `"version": "2.4.3"`; `versionCode` chưa bao giờ rời khỏi 19.
- Máy ở Vietnam reset giữa run 40 phút ngày 17/08 — vẫn cần dòng `rst:` từ Serial để biết
  nguyên nhân. File `.elf` khớp với bản `.bin` đó đã bị ghi đè, nên backtrace không giải
  mã được. Từ nay `.elf` phải lưu kèm mỗi `.bin`.
