# 2026-07-22 — `/curve` đọc đúng độ dài run đã lưu (thay vì tin `amplification_time`)

## Vấn đề

Record run trong EEPROM (`sensor67Value` 10×130 word thô) **không lưu độ dài của chính nó**.
Khi xem lại sau reboot (`POST /reviewlast`), firmware set độ dài cho `/curve` bằng:

```cpp
_sensor6035.setLastRunLoops(parameter.amplification_time); // ForteSetting.cpp
```

= **đoán** độ dài = `amplification_time` **cấu hình hiện tại**. Nếu config đổi giữa lúc chạy
run và lúc review:
- config **lớn hơn** run thật → `/curve` đọc quá, đuôi là rác (`0xFFFF`=65535 virgin hoặc 0)
  → calibrate ra giá trị khổng lồ → **trục Y chart nổ tung**, đường thật ép phẳng.
- config **nhỏ hơn** → **chart cụt**.

`RECORDPOS` chỉ ghi khi run **finished đầy đủ** (run dừng sớm không ghi), nên record luôn là
run chạy đủ `amplification_time` vòng **tại thời điểm chạy** — bug chỉ cắn khi **đổi
`amplification_time` giữa run và review** (edge hiếm nhưng thật).

## Fix (ForteSetting.cpp, nhánh PEND_REVIEW)

Quét record tìm **độ dài thật**, độc lập config: vòng cuối cùng mà slot-0 raw hợp lệ
(`10..60000`). Vòng chưa ghi đọc `0` (run-end zero-init staging buffer) hoặc `0xFFFF` (virgin).

```cpp
uint8_t len = 0;
for (uint8_t j = 0; j < 130; j++)
{
    uint16_t v = _sensor6035.sensor67Value[0][j];
    if (v > 10 && v < 60000)
        len = j + 1;
}
_sensor6035.setLastRunLoops(len);
```

- config lớn hơn: vòng sau run = 0 → không tính → `len` = độ dài thật, không đọc rác.
- config nhỏ hơn: quét vẫn tới hết data thật → `len` = độ dài thật, không cụt.
- Serial log kèm số vòng: `[review] reloaded last run from EEPROM (N rounds)`.

Log chẩn đoán nhánh fail (`[review] no stored run: probe=%u`) giữ nguyên.

## Kiểm chứng

- `g++ -O2 -std=c++17 tools/test_curve_length.cpp -o t && ./t` → **PASS**. Test mirror đúng
  vòng quét, dựng record n vòng data + đuôi rác (`0`/`0xFFFF`), chứng minh quét trả `n` (không
  phải `amplification_time`) — kể cả case run 40 vòng nhưng config 120.
- `pio run -e esp32dev` → **SUCCESS** (RAM 22.9%, Flash 68.8%).

## Nạp

Chỉ đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx`.
