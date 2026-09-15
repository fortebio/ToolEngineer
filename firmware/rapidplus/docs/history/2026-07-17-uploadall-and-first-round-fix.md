# 2026-07-17 — Target `uploadall` + sửa chart hiện data run cũ ở vòng đo đầu

## 1. `pio run -e esp32dev -t uploadall`

Nạp **firmware + `data/` trong một lệnh**. PlatformIO **không có target gộp sẵn**
(`upload` và `uploadfs` là hai builder với hai image khác nhau), nên đăng ký custom
target: `tools/pio_upload_all.py` + `extra_scripts = post:tools/pio_upload_all.py`.

```python
env.AddCustomTarget(name="uploadall", dependencies=None, actions=[
    '"$PYTHONEXE" -m platformio run -e $PIOENV -t upload',
    '"$PYTHONEXE" -m platformio run -e $PIOENV -t uploadfs',
], ...)
```

- Mỗi action **gọi lại `pio run`**; process con nạp script này nhưng chỉ để **đăng ký
  target**, không chạy → **không đệ quy**.
- Dùng `$PYTHONEXE -m platformio` thay vì `pio`: không phụ thuộc `pio` có trong PATH.
- `$PIOENV` → chạy được với env bất kỳ, không hardcode `esp32dev`.
- Thứ tự **firmware → filesystem**: upload fs reboot board sau cùng, máy khởi động lên
  với firmware mới **và** `data/` mới.

Kiểm: `--list-targets` hiện `uploadall (Custom)`; chạy thật → `firmware.bin` rồi
`littlefs.bin`, cả hai SUCCESS trong một lệnh. Build thường không bị ảnh hưởng.

## 2. BUG: chart hiện dữ liệu run cũ ngay khi amp vừa chạy

Người dùng phát hiện — **lỗ hổng còn sót trong fix trước của tôi**
([2026-07-17-result-tab-slot-naming.md](2026-07-17-result-tab-slot-naming.md) mục 4b).

`handleCurve` fallback về `lastRunLoops` **bất cứ khi nào `COUNTER == 0`**. Nhưng
`COUNTER` **cũng bằng 0 trong ~20 giây đầu của một run mới** (trước khi vòng 1 hoàn tất):

```
Bấm Start -> eoptoreading, COUNTER = 0
  -> client thay phase "amplification" -> curveReady = true -> loadCurve()
  -> GET /curve -> n = COUNTER = 0 -> FALLBACK -> tra 120 diem cua RUN CU
  -> chart mo ra day du lieu cua run truoc, roi new_readings chong len
```

Fix trước chỉ chặn ở `waitamp` (phía client, `curveReady`). Khe hở nằm ở **đầu
amplification** — nơi client **phải** gọi `/curve` (để backfill nếu vào muộn).

**Gốc rễ** vẫn là `sensor6035::clear()` gần như không bao giờ chạy → `lastRunLoops` sống
xuyên run. Sửa đúng chỗ: fallback chỉ dành cho lúc **xem lại**, không phải lúc **đang chạy**.

```cpp
uint8_t n = _sensor6035.getCurrentLoop();
if (n == 0 && _displayCLD.type_infor != eoptoreading)   // <-- guard moi
  n = _sensor6035.getLastRunLoops();
```

Đang `eoptoreading` thì `COUNTER` là **sự thật duy nhất**: 0 nghĩa là *chưa có gì*, không
phải *lấy run cũ ra*.

### Mock phải tái hiện được khe hở

`curve_count()` trước đây `if phase == "amplification" and rounds > 0: return rounds` —
`and rounds > 0` khiến mock **rơi vào fallback y như bug**, nên nó vô tình đúng và không
lộ. Sửa thành `if phase == "amplification": return rounds` (kể cả 0). Selftest assert
thẳng: `curve_count("amplification", 0, True) == 0`.

### Kiểm chứng (CDP, chạy 2 run liên tiếp)

| Thời điểm | `/curve` | Chart Home |
| --- | --- | --- |
| Run A xong | 44 | 44 điểm |
| Run B — waitamp | **44** (run A — đúng cho Result) | **0** |
| Run B — **giây đầu amp** | **1** (không phải 44) | **1** |
| Run B — sau 4s | — | 4, tăng dần |

**5/5 PASS.** Build SUCCESS (RAM 22.9%, Flash 68.6%). Đã nạp bằng `uploadall`.

## Bài học

Hai fix trước đều đúng hướng nhưng **chặn chưa đủ sâu**: `lastRunLoops` là dữ liệu
"của run nào" mà lại không có ai đánh dấu vòng đời. Vì `clear()` không chạy, mọi chỗ đọc
nó **phải tự hỏi "máy có đang chạy không"** trước khi tin.
