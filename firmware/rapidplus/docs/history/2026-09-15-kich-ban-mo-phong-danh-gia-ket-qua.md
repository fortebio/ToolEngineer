# 2026-09-15 — Kịch bản mô phỏng đánh giá thuật toán gọi kết quả, nạp thẳng vào máy qua COM

## Vì sao

Mọi tài liệu thuật toán từ v2.4.3a tới nay đều kết bằng một dòng: *"CHƯA chạy trên máy thật"*
([2026-08-13](2026-08-13-thuat-toan-goi-ket-qua-v2.4.3a.md), [2026-08-15](2026-08-15-quy-tac-hinh-dang-va-tach-nguong-jump.md),
[2026-09-11](2026-09-11-dieu-kien-tra-error-tang-som.md)). Lý do rất thực tế: một run thật tốn 30 phút
nhiệt cho 10 giếng, và **một giếng có sự thật biết trước** — bậc điện đúng 12 đơn vị, Ct đúng 2,7 phút,
một đường tăng từ trước mốc phát hiện — **không mua được từ kit thuốc thử**. Nên nhánh `F` lý do 2,
nhánh `E` không tay trái, cửa `B` sau khi có `neutralise_climbs`… đều chỉ mới được chứng minh trên PC
bằng phát lại log, chưa bao giờ trên chính firmware đang chạy trong máy.

Yêu cầu (Kane, 15/09): *tập trung lên kịch bản mô phỏng test case cho việc đánh giá kết quả; mô phỏng
các bộ dữ liệu sẽ nạp vào máy thực tế, hoặc COM7; các bộ dữ liệu phải sát với thực tế.*

## Trước

- Đường nạp đã có sẵn trong firmware từ lâu nhưng không ai dùng có hệ thống: `{"Slot":[...]}N#` qua
  UART → `ForteSetting::start_amplification_simulation()` ghi thẳng **RAW** vào `sensor67Value[N]` +
  EEPROM; `getResult` → `escreenReview` → `bResultGet()` in `Outcome check:` + JSON từng slot ra Serial
  (**không upload** — chỉ `'f'` mới upload). `tools/send_slots.py` gửi được một file, không kỳ vọng,
  không so sánh, không báo cáo.
- Dữ liệu duy nhất để nạp là `tools/slots.txt` (một run thật, đơn vị calibrate) — tức là **một** trường
  hợp, và không biết đáp án đúng của từng giếng.

## Nay

Ba file + một thư mục dữ liệu, tất cả stdlib:

| | |
| --- | --- |
| `tools/sim_cases.py` | catalogue 9 kịch bản tổng hợp + 2 run thật, bộ sinh đường cong, **MIRROR** thuật toán để pre-screen |
| `tools/simcases/` | **sinh ra, commit** — mỗi kịch bản `.cal.txt` (calibrate, phát lại được trên web mock), `.raw.txt` (đếm thô ở slope danh nghĩa, cho `send_slots.py`), `.expect.json`; `README.md` là bảng tra |
| `tools/run_sim_cases.py` | nạp toàn bộ lên máy qua COM, đọc lời máy, chấm, viết `docs/reports/simcases/<ngày>-<máy>.md` + log serial |
| `tools/test_sim_cases.py` | guard host-side: dataset đúng ý đồ, file commit không lệch `gen`, ngưỡng đọc từ source, giới hạn phần cứng |

```bash
python tools/sim_cases.py list                  # 110 giếng, ý đồ từng giếng
python tools/sim_cases.py screen -v             # MIRROR: mọi giếng đúng nhánh, ở CẢ 5 slope của đội máy
python tools/sim_cases.py gen                   # sinh lại tools/simcases/
python tools/run_sim_cases.py COM7              # ~6 phút, máy phải RẢNH ở màn chính
python tools/run_sim_cases.py COM7 --only S06   # một kịch bản
python tools/run_sim_cases.py COM7 --regrade docs/reports/simcases/<log>.serial.log   # chấm lại, không cần máy
python tools/sse_test_server.py --slots tools/simcases/S06_breaks_and_climbs.cal.txt --reboot   # xem trên web
```

### "Sát thực tế" nghĩa là gì — mỗi con số đều đo, không chọn

| thành phần | lấy từ đâu |
| --- | --- |
| mức nền 145–560 (calibrate), raw = mức × slope, slope 1,29–1,74, origin 0 | `tools/slots.txt`, `sheet/test.json`, `ParaRead` của RPL01015 |
| nhiễu **cộng tính** ~1,5 count/lần đọc, không đổi theo mức | `probe_sensor_noise.py` (σ = −0,0001·mean + 2,06) |
| warm-up 3–8 vòng đầu leo **từ dưới** vào nền (27→57→110→134→147) | `slots.txt` slot 3; CLAUDE.md "311→427 trong 5 vòng" |
| bậc **đồng bộ** cả 10 giếng ở vòng 6 (+90 raw) | `sheet/test.json` (RPL250701) |
| trôi chậm 0–25 đơn vị/30′, có khi gãy khúc muộn (+47, gối ở ~27′) | `slots.txt` slot 8, 9, 3 |
| dương tính: logistic, độ dốc đỉnh A·k/4 = 20–60/phút (tôm), 4–8 (ASF), sau plateau còn bò +25/25′ | [2026-08-21](2026-08-21-asf-va-quy-tac-hinh-dang.md), `slots.txt` slot 6 |
| xung một lần đọc, dropout 2–3 lần đọc, offset 8–40, bậc trong 5 lần đọc cuối | [2026-08-15](2026-08-15-quy-tac-hinh-dang-va-tach-nguong-jump.md), [2026-08-16](2026-08-16-min-sharpness-8-tail-climb-window-rate.md) |

Đường cong được **lượng tử hoá như ADC**: giá trị gửi lên máy là số nguyên `round(cal × slope + origin)`,
và runner **sinh lại toàn bộ theo đúng slope/origin/số vòng/ms-mỗi-vòng của máy đang cắm** (đọc bằng
`ParaRead`), không gửi file `.raw.txt` đã commit.

### Catalogue

| id | hỏi máy điều gì |
| --- | --- |
| S01 thang pha loãng | 8 P sạch Ct 5→20′ + 2 NTC: Ct có đúng ±1′, ranh P/S |
| S02 âm tính thực địa | 10 kiểu âm tính máy thật ghi được: trôi, gối muộn, warm-up lớn, bậc đồng bộ, nhiễu 3,8, bump +7 |
| S03 biên Slight Positive | Ct 20,5 / 21,3 / 22,7 / 24 / 26 / 27,5 quanh `min_slight_positive_time` 22 |
| S04 yếu + dải ngưỡng | mỗi giếng một phía của một cổng (25 / 8); dải F giữa 20/5 cũ và 25/8 mới; động học ASF |
| S05 biên phát hiện | Ct 4,0 / 3,4 (P); hai giếng **F lý do 2** (Ct ~1,7 và ~2,7 có tay trái); hai giếng E (không tay trái); rise xong trước 4′ |
| S06 bậc/xung/dropout trên âm | mỗi hàng của bảng "B hay vá": +32 sạch, +12, +19,3 (RPL01004), xung +26, dropout −40, bậc thang 3×18, −30, bậc đuôi, +32 σ3, +40 σ4,5 |
| S07 cùng artefact trên DƯƠNG | offset/dropout/bậc đuôi/bậc sau plateau/bậc trước rise/xung trên sườn: Ct phải sống qua mọi phép vá |
| S08 nhiễu | phẳng σ 1→6; P ở σ 2 và 4; P yếu ở σ 4 |
| S09 thành phần chung | 10 giếng cùng lắc + bậc chung, 2 giếng dương |
| R01 | run thật RPL250701 (raw, slope riêng), máy đã gọi N×10 |
| R02 | `tools/slots.txt`, 1 P sạch, 3 giếng trôi "người đọc không gọi được" → chỉ báo cáo |

Ba trường kỳ vọng, cố ý tách: **`expect`** = chữ theo ý đồ thiết kế · **`accept`** = chữ khác cũng được
(giếng cố ý đặt sát biên) · **`known`** = máy hôm nay trả gì khi **khác** ý đồ — điểm yếu đã biết,
không tính đậu/rớt, để khi sửa xong thấy máy dời về `expect`.

### MIRROR — là gì và không là gì

`sim_cases.py` chép lại `Algo.cpp` + luồng từng slot của `bResultGet()` **của nhánh này** (cắt cửa sổ
`+ breakIndex` trần, không có `+ JUMP_SETTLE_SKIP` và cổng `RISING_WINDOW` của nhánh `v244-alg`) để
**chọn tham số recipe có biên** trước khi đốt vào máy. Nó **không phải bằng chứng về firmware**: máy và
mirror lệch nhau thì máy là phép đo, mirror là thứ phải sửa. Ngưỡng đọc từ `define.h`/`Algo.h` lúc
import (guard kiểm là đọc được thật, không âm thầm rơi về mặc định); **logic** thì vẫn trôi được, và
chỉ máy mới bắt được điều đó.

Hai điều pre-screen bắt buộc, học từ chính lần chạy đầu:

- **Nhìn dữ liệu như máy nhìn** (`device_view`): raw nguyên → `(float(raw) − origin)/slope` bằng
  **float32** (`sensor6035.cpp:293`). Giếng S07 slot 1 có bậc +15 **bằng đúng** `4 × range` 8 điểm trước
  nó tới bit cuối của float32 trên RPL01015 → máy không vá, mirror double thì vá. Không phải lỗi máy,
  là recipe không có biên.
- **Quét 5 slope** (1,0 / 1,3 / 1,5 / 1,74 / 2,2): lượng tử hoá khác nhau theo slope, recipe phải đứng
  vững ở mọi máy nó có thể gặp. Bậc thang 3×15 trượt ở slope 2,2 → nâng thành 3×18.

## Bằng chứng — RPL01015, 15/09/2026, 11:23 (para V1.301015, 90 × 20 s, ngưỡng khớp `define.h`; `ParaRead` không nói version firmware — hành vi Ct 3,33 → P cho thấy bản có `d7775b1`)

Log: [`docs/reports/simcases/2026-09-15-1123-RPL01015.serial.log`](../reports/simcases/2026-09-15-1123-RPL01015.serial.log)
· báo cáo chấm lại: [`2026-09-15-1146-RPL01015-regrade.md`](../reports/simcases/2026-09-15-1146-RPL01015-regrade.md).

**110 giếng: 100 PASS · 0 FAIL · 1 KNOWN-WEAK · 3 INFO · 6 STALE** (6 giếng đã đổi recipe sau khi
đo, chờ nạp lại — với dữ liệu cũ cả 6 đều đúng chữ, chỉ thiếu biên hoặc thiếu nhánh F/2). Máy và mirror **cùng chữ ở 110/110**, cùng Ct tới một vòng ở mọi giếng P/S/F/E
(một ca lệch đúng một vòng: S01 slot 7, 18,00 vs 17,67); Ct của giếng N lệch tuỳ ý vì đó chỉ là đỉnh
nhiễu, và float32 quyết định bump nào là đỉnh.

Những điều máy nói mà tài liệu trước đây chỉ suy luận:

1. **`MIN_CALLABLE_CT = 3.0` đang sống trên máy**: Ct 3,33 → **P**, Ct 3,00 → **P** (`d7775b1`).
   Nhánh F lý do 2 **chưa** được kích — giếng hai pha bản đầu rơi đúng Ct 3,00; hai giếng mới (Ct ~1,7
   và ~2,7, robust qua 5 seed × 5 slope trên mirror) **chờ nạp lại**.
2. **Bậc +32 sạch trên nền phẳng → N (đã vá), KHÔNG phải B.** `neutralise_climbs` chạy trước
   `check_breakData` và không có trần trên (`CLIMB_MIN_STEP` 8, không có max) nên vá luôn. `B` chỉ còn
   với tới được khi bậc nằm trong dải **`2,5×range ≤ jump < 4×range`** của 8 điểm trước nó (nền nhiễu):
   +32 trên σ 3 → **B** (đo được). Đây là hình dạng thật của cổng B sau 2026-08-15, chưa ai viết ra.
3. **+40 trên σ 4,5 → P** (`known`): lọt cổng climb (nhiễu lớn hơn 1/4 bậc) **và** lọt `checkJump`
   (độ dốc sau bậc > 1,0 vì nhiễu), tới bộ chấm bị SG làm mượt thành vai: increase 48, sharpness 30.
   Chính họ lỗi RPL01004, trên giếng nhiễu. Ghi là điểm yếu, không giấu.
4. **Rise kết thúc trước 4′ là VÔ HÌNH → N** (S05 slot 10: Ct 2,0, k 2,5): cửa sổ nền [2,4) nằm trên
   sườn lên, đỉnh đạo hàm tìm từ mẫu 12 chỉ thấy đuôi. Không phải E, không phải F.
5. **Ct của thuật toán sớm hơn "Ct 40% biên độ"** mà mắt người ước: `slots.txt` slot 6 là **4,33**, không
   phải ~6,7 — Ct là nơi đạo hàm tụt còn 40% đỉnh khi đi ngược, tức chân sườn. Kỳ vọng R02 sửa theo.
6. **JSON Serial của `bResultGet()` luôn in `climbs_fixed 0 / climb_first_i −1`**: `toJSON()` ở
   `sensor6035.cpp:418` chạy **trước** hai phép gán ở `:427-428`. Đường upload gán trước khi copy
   (`:613-616`) nên payload đúng; chỉ bản in debug sai. Runner đếm climb từ dòng
   `Slot N: neutralised K vertical climb(s)`. **Sửa firmware = dời hai dòng gán lên trên `toJSON()`** —
   chưa làm trong lần này (đổi firmware là một commit riêng, và máy đang cắm dùng để đo).
7. **Serial của máy đan xen ở mức byte**, không phải mức dòng: `[stack]` + ` Control=2128/4096` +
   … là 8 lần `Serial.print` riêng (`main.cpp:471-480`), `[len] set(90)…`, `finish one round
   maintenance`, `[dash] heap…` rơi vào **giữa hai chữ số** của JSON; lần đầu 9/120 record đọc hỏng.
   Parser nay: gỡ các mảnh **biết trước** rồi quét tiền tố JSON theo schema (mảng phẳng, chuỗi chỉ
   `[A-Za-z0-9_ ]`) để cắt đúng chỗ text lạ bắt đầu và nối phần còn lại ở dòng sau → 120/120.

## Đã loại

- **Link thẳng `Algo.cpp` host-side như `audit_logs.py`** — máy này không có g++/MSVC/Docker daemon.
  Mirror Python là phương án còn lại, và được đặt đúng chỗ của nó: pre-screen, không phải oracle.
- **Regex danh sách text lạ đơn thuần** — đủ cho `[dash]`, hỏng ngay với `[stack]` in 8 mảnh. Giữ danh
  sách cho mảnh **không có xuống dòng** (không thể đoán được điểm kết thúc), phần còn lại cho scanner.
- **Ô nhập kỳ vọng bằng tay / nhãn kỹ sư** — đây không phải bộ nhãn; `expect` là ý đồ thiết kế giếng.
- **Restore record bằng `EEPROMRead`** — `sprintf(tmp[4], "%02X", (char)c)` tràn buffer với byte ≥ 0x80.
  Dùng `raw_data` từ `getResult` (sau `neutralise_climbs`, có ghi rõ trong báo cáo khi run cũ có climb).

## Còn nợ

- **Nạp lại 6 giếng đã đổi recipe** (S05 slot 3, 8 · S06 slot 2, 6 · S07 slot 1, 8) — COM7 rớt giữa
  lần chạy thứ hai (11:35). Record run thật của RPL01015 hiện **chưa được trả lại** (lần 1 backup
  hỏng vì parser): `python tools/run_sim_cases.py COM7 --restore-from
  docs/reports/simcases/2026-09-15-1123-RPL01015.serial.log` sẽ nạp cả catalogue rồi trả record.
- Dời hai dòng gán `climbs_fixed`/`climb_first_i` lên trước `toJSON()` trong `bResultGet()`.
- Nhánh `a` (`SHAPE_RULE_NEGATIVE`): mirror có `--variant a`, chưa có máy nào cắm bản đó để đo.
- ASF: S04 slot 4 (sharp 6,6, inc 52) → **F** đúng như [2026-08-21](2026-08-21-asf-va-quy-tac-hinh-dang.md)
  cảnh báo — bộ này **chứng minh** cái giá đó trên máy, không giải quyết nó.
