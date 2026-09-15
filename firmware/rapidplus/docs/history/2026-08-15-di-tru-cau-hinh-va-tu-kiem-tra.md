# Di trú cấu hình v2.4.3a + tự kiểm tra khi khởi động (2026-08-15)

## Vấn đề

Sửa giá trị mặc định trong `define.h` **không tới được máy đã cấu hình**.

`ForteSetting::begin()` đọc `parastructure` từ EEPROM và nếu `length == sizeof(parameter)` thì
**ghi đè toàn bộ** giá trị biên dịch:

```c
if (paraEEPROM.length == sizeof(parameter))
    parameter = paraEEPROM;   // keep the user's saved configuration across reboots
```

`sizeof(parastructure)` **không đổi** từ v2.4.2 (vẫn 400 B, trần 402 B), nên khối EEPROM cũ vẫn
hợp lệ và mọi máy ngoài hiện trường giữ nguyên **120 vòng / min_increase 20 / min_sharpness 5**.

Đây không phải lệch nhỏ về hình thức. `removed_by_new_gate()` (Algo.cpp) so **cặp runtime** với
`LEGACY_MIN_*`:

```c
const bool passedLegacy = (oc.increase > LEGACY_MIN_INCREASE) && (peak.y > LEGACY_MIN_SHARPNESS);
const bool passesNow    = (oc.increase > parameters.min_increase) && (peak.y > parameters.min_sharpness);
return passedLegacy && !passesNow && lagOk;
```

Máy còn giữ 20/5 thì hai vế **luôn bằng nhau** → hàm luôn trả `false` → **không giếng nào bị gọi
`F` hay lật sang `N`**. Cả hai bản build sẽ chạy, báo kết quả bình thường, và tính năng **im lặng
tắt hẳn**. Cùng lý do, `baseline_start`/`baseline_range` vẫn là 3/4 chứ không phải 2/2, phá bất
biến `baseline_start + baseline_range == detection_margin_time`.

## Cách sửa: di trú một lần, đóng dấu theo REVISION

`define.h`:

```c
#define ADDR_CONFIG_REV 236       // khe trống: ADDR_CHECK_UPDATE 228 (+4B) .. ADDR_ERROR_NUMBER_UNIT 244
#define CONFIG_REV_THRESHOLDS 1   // tăng số này để ép một bộ giá trị mới ở bản sau
```

`ForteSetting::begin()`, **bên trong nhánh cấu hình hợp lệ**, ngay sau seed `kpid3`:

| Trường | Cũ | Mới |
| --- | --- | --- |
| `lysisDuration` | 600 s | 600 s (10 phút) |
| `amplification_time` | 120 vòng (40 phút) | **90 vòng (30 phút)** |
| `min_increase` | 20.0 | **25.0** |
| `min_sharpness` | 5.0 | **11.0** |
| `detection_margin_time` | 4.0 | 4.0 |
| `baseline_start` / `baseline_range` | 3 / 4 | **2 / 2** |
| `arm_percentile` | 0.9 | **0.5** |

### Bốn điểm bắt buộc

1. **`EEPROM.read()` và chuẩn hoá `0xFF` TRƯỚC phép so.** Byte trắng đọc ra 255; với
   `if (rev < CONFIG_REV_THRESHOLDS)` thì `255 < 1` là **false** → bỏ qua di trú đúng trên những
   máy cần nó nhất. Đây chính là bẫy `readBool()` của di trú device-ID slot 170, đội lốt khác.
2. **Đóng dấu MỘT LẦN, không so giá trị.** 120 vòng là một thiết lập hợp lệ ai đó có thể đã chọn;
   so giá trị sẽ ghi đè lại mỗi lần khởi động, mãi mãi. (Seed `kpid3` bên cạnh **được phép** so
   giá trị vì `{0,0,0}` là bất khả thi với một PID thật.)
3. **KHÔNG gate theo `FirmwareVer`** — global đó đổi mỗi bản, máy nhảy 2.4.2 → 2.4.4 sẽ bỏ qua.
4. **Hiệu chuẩn không được đụng tới.** `slopes` (0.50–3.50 toàn fleet), `origins` và `led_power`
   là **của riêng từng máy**. Chúng được `memcpy` ra trước khi sửa, `memcmp` lại sau, và bất kỳ
   sai khác nào **huỷ toàn bộ lần ghi**:

```c
if (calibIntact) { EEPROM.put(PARAMETERPOS, parameter); EEPROM.write(ADDR_CONFIG_REV, ...); }
else             { /* khôi phục + "MIGRATION ABANDONED", không ghi gì */ }
```

Lý do khắt khe: máy mất hiệu chuẩn **vẫn chạy bình thường** và **mọi con số nó báo đều sai** —
tệ hơn hẳn một máy chưa di trú, vốn còn cứu được.

## Tự kiểm tra

`configSelfCheckJson()` / `configSelfCheckLog()` (ForteSetting.cpp), phục vụ qua **`GET /selfcheck`**
và in ra Serial ở cuối `begin()`.

- Đọc **struct `parameter` đang sống**, không đọc mặc định biên dịch — đọc mặc định chính là sai
  lầm mà nó sinh ra để bắt.
- Báo **thời gian ly giải và thời gian gọi kết quả bằng PHÚT** (struct lưu giây và vòng), quy đổi
  bằng **`timePerLoop` của chính máy** chứ không hardcode 20 s.
- Kiểm `review gate LIVE/INERT`: cặp runtime phải **vượt** `LEGACY_MIN_*`, nếu không tính năng
  tắt. Một máy có thể đạt mọi dòng khác mà vẫn INERT.
- Kiểm bất biến `baseline_start + baseline_range == detection_margin_time`.
- **Hiệu chuẩn được BÁO CÁO, không chấm điểm**: không có giá trị đúng chung cho cả fleet. Chỉ nói
  được nó có còn là placeholder biên dịch (`slopes` toàn 1.0) hay không, và cờ đó **không** tính
  vào pass/fail — máy mới chưa hiệu chuẩn không phải là di trú hỏng.
- Handler **không mở EEPROM** (chạy trên AsyncTCP — CLAUDE.md Setting #2); byte revision được cache
  vào RAM lúc boot.

## Guard

`python tools/test_config_migration.py` — 7 nhóm kiểm tra. Negative test **5/5 đỏ đúng chỗ**:
bỏ chuẩn hoá `0xFF`, hạ `min_sharpness` về 5.0, bỏ một `memcmp`, đưa `EEPROM.put` lên trước
`calibIntact`, gate theo `FirmwareVer`.

## Ghi chú

- Áp dụng ở **lần boot đầu sau khi nạp**, kể cả nạp qua OTA — không cần thao tác riêng từng máy.
- Đóng dấu một lần nên sau đó **người vận hành vẫn tự chỉnh được** và giá trị sẽ giữ.
- `tools/test_profile_minutes.js` hiện lỗi ở `realPerLoop` (harness tự trích thân hàm
  `perLoopMs()` rồi eval); **không liên quan** thay đổi này — không file JS nào bị sửa.
- `test_upload_targets.py` mà CLAUDE.md nhắc tới **không tồn tại** trong `tools/`.
